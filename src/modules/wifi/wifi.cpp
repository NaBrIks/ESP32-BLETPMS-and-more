// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
#include "wifi.hpp"
#include "modules/web/web.hpp"
#include <NaWFstore/NaWFstore.hpp>
#include <NaWFstore/NvsProfileStore.hpp>
#include <NaWFstore/EspIdfWifiDriver.hpp>
#include <NaWFstore/Console.hpp>
#include <esp_random.h>
#include <esp_mac.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <mutex>
#include <unistd.h>
#include <termios.h>
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#endif
#if __has_include("wifi_secret.hpp") && NAWFSTORE_WITH_BOOTSTRAP
#include "wifi_secret.hpp"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#define WIFI_PASS ""
#endif
#ifndef WIFI_ALT_SSID
#define WIFI_ALT_SSID ""
#define WIFI_ALT_PASS ""
#endif
using application::Value;
using application::member;
using application::string_value;
using application::integer_value;
namespace
{
#if NAWFSTORE_WITH_NVS
	NaWFstore::NvsProfileStore store;
#else
	class VolatileStore final : public NaWFstore::IProfileStore
	{
	public:
		NaWFstore::LoadResult load(NaWFstore::Config &, std::string &) override
		{
			return NaWFstore::LoadResult::missing;
		}

		bool save(const NaWFstore::Config &, std::string &) override
		{
			return true;
		}

	} store;
#endif
	NaWFstore::EspIdfWifiDriver driver;
	NaWFstore::EspClock clock_source;
	NaWFstore::Manager manager(store, driver, clock_source);
	std::mutex guard;
	struct Pending { uint32_t ticket; Value command; uint64_t not_before; };
	std::deque<Pending> commands;
	std::deque<Value> results;
	uint32_t next_ticket{}, revision{}, in_flight{};
	Value status_snapshot, profiles_snapshot;
	std::string address = "offline", ap_text;
	NaWFstore::ApConfig ap_config;
	bool configured_enabled{};
	uint32_t configured_seconds{};

	bool boolean(const Value &value, bool fallback)
	{
		const auto *found = std::get_if<bool>(&value.value);
		return found ? *found : fallback;
	}

	void publish(bool journal_transitions = false)
	{
		const auto status = manager.status();
		web_network_enabled(WebNetwork::WifiStation, status.enabled);
		web_network_enabled(WebNetwork::WifiAp, status.driver.ap_active);
		const auto &config = manager.config();
		Value::Array profiles;
		for(const auto &profile : config.profiles)
			profiles.push_back(Value::Object{{"id", profile.id}, {"ssid", profile.ssid},
					{"priority", profile.priority}, {"enabled", profile.enabled}});
		Value public_status = Value::Object{{"enabled", status.enabled}, {"connected", status.driver.connected},
			{"disconnects", status.driver.disconnects}, {"starts", status.driver.starts},
			{"stops", status.driver.stops},
			{"webEnabled", bool(NAWFSTORE_WITH_WEB)}, {"apSupported", bool(NAWFSTORE_WITH_SOFTAP)},
			{"ready", status.ready}, {"stopping", false}, {"rssi", status.driver.rssi},
			{"ip", status.driver.ip}, {"profile", status.current_profile}, {"attempts", status.attempts},
			{"lastReason", status.driver.last_reason}, {"error", status.error},
			{"remainingSeconds",
			 status.enabled ? (status.remaining_seconds ? int64_t(status.remaining_seconds) : -1) : 0},
			{"windowSeconds", config.startup_window_seconds}, {"apActive", status.driver.ap_active},
			{"apSsid", status.driver.ap_active ? ap_config.ssid : ""}, {"apIp", status.driver.ap_ip},
			{"apRemainingSeconds", status.ap_remaining_seconds}};
		if(journal_transitions)
		{
			static NaWFstore::Status previous;
			const Value diagnostic = Value::Object{{"enabled", status.enabled}, {"connected", status.driver.connected},
				{"attempts", status.attempts}, {"disconnects", status.driver.disconnects},
				{"starts", status.driver.starts},
				{"stops", status.driver.stops}, {"lastReason", status.driver.last_reason}, {"rssi", status.driver.rssi},
				{"apActive", status.driver.ap_active}, {"remainingSeconds", status.remaining_seconds},
				{"ip", status.driver.ip}, {"apIp", status.driver.ap_ip}};
			auto emit = [&](const char *event) {
					ESP_LOGI("APP_WIFI", "%s ip=%s connected=%u reason=%u", event, status.driver.ip.c_str(),
					    unsigned(status.driver.connected), unsigned(status.driver.last_reason));
					application::journal(lvgl_cbor::data_value_json(Value::Object{{"event", event},
							{"timeMs", clock_source.milliseconds()}, {"unixSeconds", int64_t(time(nullptr))},
							{"wifi", diagnostic}, {"result", 0}}));
				};
			if(status.driver.starts != previous.driver.starts) emit("wifi-started");

			if(status.attempts != previous.attempts) emit("wifi-connect-attempt");

			if(status.driver.disconnects != previous.driver.disconnects) emit("wifi-disconnected");

			if(status.driver.connected && !previous.driver.connected) emit("wifi-connected");

			if(status.driver.ip != previous.driver.ip) emit("wifi-ip-changed");

			if(!status.driver.connected && previous.driver.connected &&
			    status.driver.disconnects == previous.driver.disconnects)
				emit("wifi-ip-lost");

			if(status.driver.stops != previous.driver.stops) emit("wifi-stopped");

			if(status.driver.ap_active != previous.driver.ap_active)
				emit(status.driver.ap_active ? "wifi-ap-started" : "wifi-ap-stopped");

			previous = status;
		}

		std::lock_guard<std::mutex> lock(guard);
		status_snapshot = std::move(public_status);
		profiles_snapshot = Value::Object{{"profiles", profiles}, {"startupEnabled", config.startup_enabled},
			{"startupWindowSeconds", config.startup_window_seconds}, {"revision", revision}};
		address = status.driver.connected ? status.driver.ip : "offline";
		ap_text = status.driver.ap_active ? ap_config.ssid + " / " + ap_config.password +
		    " / http://" + status.driver.ap_ip + ":80/setup" : "AP off";
	}

	bool execute(const Value &command, std::string &error)
	{
		const auto op = string_value(member(command, "op"));
		const auto id = integer_value(member(command, "id"));
		const auto seconds = integer_value(member(command, "seconds"), 0);
		if(seconds < 0 || seconds > 86400) { error = "seconds_range"; return false; }

		if(op == "put")
		{
			NaWFstore::Profile profile;
			if(id < 0 || id > UINT32_MAX) { error = "id_range"; return false; }

			profile.id = uint32_t(id);
			const auto &profiles = manager.config().profiles;
			const auto existing = std::find_if(profiles.begin(), profiles.end(), [id](const auto &p){
				return p.id == id;
			});
			if(existing != profiles.end()) profile = *existing;

			profile.ssid = string_value(member(command, "ssid"));
			if(std::holds_alternative<std::string>(member(command, "password").value))
				profile.password = string_value(member(command, "password"));

			profile.enabled = boolean(member(command, "enabled"), profile.enabled);
			const auto priority = integer_value(member(command, "priority"), profile.priority);
			if(priority < INT16_MIN || priority > INT16_MAX) { error = "priority_range"; return false; }

			profile.priority = int16_t(priority);
			return manager.put_profile(std::move(profile), error);
		}

		if(op == "priority" || op == "profile.enabled")
		{
			for(auto profile : manager.config().profiles)
			{
				if(profile.id != id) continue;

				if(op == "priority")
				{
					const auto priority = integer_value(member(command, "priority"));
					if(priority < INT16_MIN || priority > INT16_MAX) { error = "priority_range"; return false; }

					profile.priority = int16_t(priority);
				}
				else profile.enabled = boolean(member(command, "enabled"), true);

				return manager.put_profile(profile, error);
			}
			error = "profile_not_found";
			return false;
		}

		if(op == "remove") return manager.remove_profile(uint32_t(id), error);

		if(op == "connect") return manager.connect(uint32_t(id), error);

		if(op == "startup") return manager.set_startup(boolean(member(command, "enabled"), true), seconds, error);

		if(op == "on") return manager.enable(seconds, error);

		if(op == "off") return manager.disable(error);

		if(op == "ap.start")
		{
			ap_config.seconds = seconds ? seconds : 300;
			return manager.start_ap(ap_config, error);
		}

		if(op == "ap.stop") return manager.stop_ap(error);

		error = "unknown_command";
		return false;
	}

	void worker(void *)
	{
		for(;;)
		{
			Pending pending;
			bool found = false;
			{
				std::lock_guard<std::mutex> lock(guard);
				if(!commands.empty() && clock_source.milliseconds() >= commands.front().not_before)
				{
					pending = std::move(commands.front()); commands.pop_front(); found = true;
					in_flight = pending.ticket;
				}
			}
			if(found)
			{
				std::string error;
				const bool success = execute(pending.command, error);
				if(success) ++revision;

				std::lock_guard<std::mutex> lock(guard);
				results.push_back(Value::Object{{"id", pending.ticket}, {"state", success ? "done" : "failed"},
						{"error", error}});
				in_flight = 0;
				if(results.size() > 16) results.pop_front();
			}

			manager.poll();
			publish(true);
			vTaskDelay(pdMS_TO_TICKS(50));
		}
	}

#if NAWFSTORE_WITH_CONSOLE
	std::string console_command(const std::vector<std::string> &arguments)
	{
		if(arguments.empty() || arguments[0] == "help")
			return
			    "wifi status | web | list | add | edit ID | remove ID | connect ID | on [seconds] | off | startup on/off [seconds] | ap start [seconds] | ap stop | result ID";

		const auto &op = arguments[0];
		if(op == "web")
		{
			if(arguments.size() == 1) return lvgl_cbor::data_value_json(web_probe());

			if(arguments.size() == 2 && arguments[1] == "status") return lvgl_cbor::data_value_json(web_status());

			if(arguments.size() == 2 && (arguments[1] == "on" || arguments[1] == "off"))
			{
				web_request(arguments[1] == "on");
				return "Web request queued (session only)";
			}
			return "wifi web [status|on|off]";
		}

		if(op == "status") return lvgl_cbor::data_value_json(wifi_status());

		if(op == "list") return lvgl_cbor::data_value_json(wifi_profiles());

		if(op == "result" && arguments.size() == 2)
			return lvgl_cbor::data_value_json(wifi_operation(std::strtoul(arguments[1].c_str(), nullptr, 10)));

		Value::Object command;
		auto number = [&](size_t index, uint32_t fallback = 0) -> uint32_t {
				if(index >= arguments.size()) return fallback;

				char *end{};
				const auto result = std::strtoul(arguments[index].c_str(), &end, 10);
				return end && *end == 0 && result <= UINT32_MAX ? uint32_t(result) : UINT32_MAX;
			};
		if(op == "put" && arguments.size() == 4)
			command = {{"op", "put"}, {"id", number(1)}, {"ssid", arguments[2]}, {"password", arguments[3]}};
		else if((op == "remove" || op == "connect") && arguments.size() == 2)
			command = {{"op", op}, {"id", number(1)}};
		else if(op == "priority" && arguments.size() == 3)
		{
			char *end{};
			const auto priority = std::strtol(arguments[2].c_str(), &end, 10);
			if(!end || *end || priority < INT16_MIN || priority > INT16_MAX) return "Invalid priority";

			command = {{"op", "priority"}, {"id", number(1)}, {"priority", int64_t(priority)}};
		}
		else if((op == "enable" || op == "disable") && arguments.size() == 2)
			command = {{"op", "profile.enabled"}, {"id", number(1)}, {"enabled", op == "enable"}};
		else if(op == "on" || op == "off") command = {{"op", op}, {"seconds", number(1)}};
		else if(op == "startup" && arguments.size() >= 2 && (arguments[1] == "on" || arguments[1] == "off"))
			command = {{"op", "startup"}, {"enabled", arguments[1] == "on"}, {"seconds", number(2)}};
		else if(op == "ap" && arguments.size() >= 2 && (arguments[1] == "start" || arguments[1] == "stop"))
		{
			command = {{"op", arguments[1] == "start" ? "ap.start" : "ap.stop"}, {"seconds", number(2, 300)}};
			if(arguments[1] == "start")
				std::printf("AP %s / password %s / http://192.168.4.1:80/setup\n", ap_config.ssid.c_str(),
				    ap_config.password.c_str());
		}
		else return "Invalid command; wifi help";

		return lvgl_cbor::data_value_json(wifi_config_command(command));
	}

	void console_worker(void *)
	{
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
		// IDF 5.5.6 nonblocking VFS reads use the driver's RX ring buffer.
		// Install it before polling stdin, rather than leaving USB in polled mode.
		usb_serial_jtag_driver_config_t usb_config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
		ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_config));
		usb_serial_jtag_vfs_use_driver();
#endif
		const int fd = fileno(stdin);
		struct termios terminal {};
		if(tcgetattr(fd, &terminal) == 0)
		{
			terminal.c_lflag &= ~(ECHO | ECHONL);
			// The portable parser owns CR/LF normalization. UART VFS must not
			// translate CR into LF first, otherwise CRLF becomes two Enter keys.
			terminal.c_iflag &= ~(ICRNL | INLCR | IGNCR);
			(void)tcsetattr(fd, TCSANOW, &terminal);
		}

		(void)fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
		NaWFstore::Console console(console_command, [](const std::string &text){
		    std::fwrite(text.data(), 1, text.size(), stdout);
			std::fflush(stdout);
			});
		std::printf("NaWFstore console: wifi help\n");
		std::fflush(stdout);
		for(;;)
		{
			char bytes[64];
			const auto count = read(fd, bytes, sizeof(bytes));
			for(int index = 0; index < count; ++index)
				console.feed(bytes[index]);
			vTaskDelay(pdMS_TO_TICKS(20));
		}
	}

#endif
}

esp_err_t wifi_init(bool enabled, uint32_t seconds)
{
	configured_enabled = enabled; configured_seconds = seconds;
	std::string error;
	if(!driver.initialize(error)) { ESP_LOGE("APP_WIFI", "Driver: %s", error.c_str()); return ESP_FAIL; }

	NaWFstore::Config bootstrap;
	bootstrap.startup_enabled = enabled;
	bootstrap.startup_window_seconds = seconds;
#if NAWFSTORE_WITH_BOOTSTRAP
	if(*WIFI_SSID) bootstrap.profiles.push_back({1, WIFI_SSID, WIFI_PASS, 100, true});

	if(*WIFI_ALT_SSID && bootstrap.profiles.size() < NAWFSTORE_MAX_PROFILES)
		bootstrap.profiles.push_back({2, WIFI_ALT_SSID, WIFI_ALT_PASS, 90, true});

#endif
	if(!manager.initialize(bootstrap, error)) ESP_LOGE("APP_WIFI", "Store: %s", error.c_str());

	uint8_t mac[6]{};
	(void)esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
	char ssid[32], password[20];
	std::snprintf(ssid, sizeof(ssid), "NaWF-%02X%02X%02X", mac[3], mac[4], mac[5]);
	std::snprintf(password, sizeof(password), "%08lX%08lX", static_cast<unsigned long>(esp_random()),
	    static_cast<unsigned long>(esp_random()));
	ap_config.ssid = ssid; ap_config.password = password;
	publish();
	if(xTaskCreate(worker, "nawfstore", 6144, nullptr, 3, nullptr) != pdPASS) return ESP_ERR_NO_MEM;

#if NAWFSTORE_WITH_CONSOLE
	if(xTaskCreate(console_worker, "nawfconsole", 6144, nullptr, 2, nullptr) != pdPASS) return ESP_ERR_NO_MEM;

#endif
	return ESP_OK;
}

Value wifi_config_command(const Value &command)
{
	std::lock_guard<std::mutex> lock(guard);
	if(commands.size() >= 4) return Value::Object{{"accepted", false}, {"error", "queue_full"}};

	const auto encoded = lvgl_cbor::data_value_json(command);
	if(encoded.size() > 1024) return Value::Object{{"accepted", false}, {"error", "command_too_large"}};

	const uint32_t ticket = ++next_ticket;
	const auto op = string_value(member(command, "op"));
	const bool closes_connection = op == "off" || op == "ap.stop";
	commands.push_back({ticket, command, clock_source.milliseconds() + (closes_connection ? 2000 : 0)});
	return Value::Object{{"accepted", true}, {"id", ticket}};
}

Value wifi_operation(uint32_t id)
{
	std::lock_guard<std::mutex> lock(guard);
	if(in_flight == id && id) return Value::Object{{"id", id}, {"state", "queued"}};

	for(const auto &result : results)
		if(integer_value(member(result, "id")) == id) return result;

	for(const auto &pending : commands)
		if(pending.ticket == id) return Value::Object{{"id", id}, {"state", "queued"}};

	return Value::Object{{"id", id}, {"state", "unknown"}};
}

Value wifi_profiles()
{
	std::lock_guard<std::mutex> lock(guard);
	return profiles_snapshot;
}

Value wifi_status()
{
	std::lock_guard<std::mutex> lock(guard);
	return status_snapshot;
}

Value wifi_diagnostics()
{
	const auto status = wifi_status();
	Value::Object diagnostic;
	for(const auto *key : {"enabled", "connected", "stopping", "remainingSeconds", "windowSeconds", "attempts",
	                       "disconnects", "starts", "stops", "lastReason", "rssi", "apActive", "apRemainingSeconds"})
		diagnostic.emplace(key, member(status, key));
	return diagnostic;
}

std::string wifi_ap_credentials()
{
	std::lock_guard<std::mutex> lock(guard);
	return ap_text;
}

bool wifi_request(bool enabled, uint32_t seconds)
{
	return boolean(member(wifi_config_command(Value::Object{{"op", enabled ? "on" : "off"}, {"seconds", seconds}}),
	           "accepted"), false);
}

bool wifi_ap_request(bool enabled, uint32_t seconds)
{
	return boolean(member(wifi_config_command(Value::Object{{"op", enabled ? "ap.start" : "ap.stop"},
				   {"seconds", seconds}}), "accepted"), false);
}

void wifi_poll(bool enabled, uint32_t seconds)
{
	// Legacy FS settings only send commands on actual edits, never overwrite NVS at startup.
	if(enabled == configured_enabled && seconds == configured_seconds) return;

	const auto result = wifi_config_command(Value::Object{{"op", "startup"}, {"enabled", enabled},
				{"seconds", seconds}});
	if(boolean(member(result, "accepted"), false))
	{
		configured_enabled = enabled; configured_seconds = seconds;
		(void)wifi_request(enabled, seconds);
	}
}

void wifi_address(char (&destination)[16])
{
	std::lock_guard<std::mutex> lock(guard);
	std::snprintf(destination, sizeof(destination), "%s", address.c_str());
}
