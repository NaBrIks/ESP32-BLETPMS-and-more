// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
#include "wifi.hpp"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#if __has_include("wifi_secret.hpp")
#include "wifi_secret.hpp"
#else
#define WIFI_SSID ""
#define WIFI_PASS ""
#endif
#ifndef WIFI_ALT_SSID
#define WIFI_ALT_SSID ""
#define WIFI_ALT_PASS ""
#endif

namespace
{
	struct Network { const char *ssid; const char *password; };
	constexpr std::array<Network, 2> networks{{{WIFI_SSID, WIFI_PASS}, {WIFI_ALT_SSID, WIFI_ALT_PASS}}};
	static_assert(sizeof(WIFI_SSID) - 1 <= 32 && sizeof(WIFI_ALT_SSID) - 1 <= 32);
	static_assert(sizeof(WIFI_PASS) - 1 <= 64 && sizeof(WIFI_ALT_PASS) - 1 <= 64);
	ESP_EVENT_DEFINE_BASE(APP_WIFI_RETRY);
	std::atomic<uint32_t> current_address{};
	esp_timer_handle_t retry_timer{};
	size_t selected{};
	unsigned failed_in_cycle{};
	bool connected{};
	std::atomic<bool> powered{}, online{}, stopping{}, expiry_queued{};
	std::atomic<uint32_t> deadline_seconds{}, window_seconds{}, attempts{}, disconnects{}, starts{}, stops{},
	    last_reason{};
	uint32_t configured_window{};
	bool configured_enabled = true;
	struct Control { bool enabled; uint32_t seconds; };
	constexpr int control_event = 1, expire_event = 2;
	uint32_t seconds_now()
	{
		return uint32_t(esp_timer_get_time() / 1000000);
	}

	void radio_log(const char *event, int result = 0)
	{
		application::journal(lvgl_cbor::data_value_json(application::Value::Object{
			{"event", event}, {"timeMs", esp_timer_get_time() / 1000},
			{"wifi", wifi_status()}, {"result", result}}));
	}

	void stop_radio()
	{
		(void)esp_timer_stop(retry_timer);
		const auto result = powered ? esp_wifi_stop() : ESP_OK;
		if(result == ESP_OK || result == ESP_ERR_WIFI_NOT_STARTED)
		{
			powered = false; online = false; connected = false; current_address = 0;
			deadline_seconds = 0; stopping = false; ++stops;
		}

		expiry_queued = false;
		radio_log("wifi-stopped", result);
	}

	size_t network_count()
	{
		size_t count = 0;
		for(const auto &network : networks)
			if(*network.ssid) ++count;

		return count;
	}

	void connect_selected();

	void schedule_next()
	{
		const auto count = network_count();
		if(!count) return;

		do { selected = (selected + 1) % networks.size(); } while(!*networks[selected].ssid);
		// Try the other configured network promptly. If neither is available,
		// pause before the next pass so Wi-Fi scanning does not monopolize the radio.
		const bool full_cycle = ++failed_in_cycle >= count;
		if(full_cycle) failed_in_cycle = 0;

		(void)esp_timer_stop(retry_timer);
		ESP_ERROR_CHECK(esp_timer_start_once(retry_timer, full_cycle ? 15000000 : 250000));
	}

	void connect_selected()
	{
		if(connected || !powered || stopping || !network_count()) return;

		while(!*networks[selected].ssid)
			selected = (selected + 1) % networks.size();
		wifi_config_t station{};
		const auto &network = networks[selected];
		std::memcpy(station.sta.ssid, network.ssid, std::strlen(network.ssid));
		std::memcpy(station.sta.password, network.password, std::strlen(network.password));
		station.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
		auto result = esp_wifi_set_config(WIFI_IF_STA, &station);
		if(result == ESP_OK) { ++attempts; result = esp_wifi_connect(); }

		radio_log("wifi-connect-attempt", result);
		ESP_LOGI("APP_WIFI", "Network %u/%u: connect request %s",
		    unsigned(selected + 1), unsigned(network_count()), esp_err_to_name(result));
		if(result != ESP_OK) schedule_next();
	}

	void network_event(void *, esp_event_base_t base, int32_t id, void *payload)
	{
		// Network selection and SDK configuration are serialized on the default
		// event task. The timer only queues an event, never blocks its timer task.
		if(base == APP_WIFI_RETRY)
		{
			if(id == control_event)
			{
				const auto command = *static_cast<const Control *>(payload);
				if(!command.enabled)
				{
					// Let the authenticated HTTP acknowledgement drain before RF stops.
					stopping = true; deadline_seconds = seconds_now() + 2;
					radio_log("wifi-stop-requested");
				}
				else
				{
					stopping = false; expiry_queued = false; window_seconds = command.seconds;
					deadline_seconds = command.seconds ? seconds_now() + command.seconds : 0;
					if(!powered)
					{
						const auto result = esp_wifi_start();
						if(result == ESP_OK){powered = true; ++starts;}

						if(result == ESP_OK) (void)esp_wifi_set_ps(WIFI_PS_NONE);

						radio_log("wifi-started", result);
					}
					else if(!connected) connect_selected();

					radio_log("wifi-window");
				}
			}
			else if(id == expire_event)
			{
				// A more recent open command can supersede an already queued expiry.
				if(deadline_seconds && seconds_now() >= deadline_seconds) stop_radio();
				else expiry_queued = false;
			}
			else connect_selected();

			return;
		}

		if(base == WIFI_EVENT && id == WIFI_EVENT_STA_START) { connect_selected(); return; }

		if(base == WIFI_EVENT && id == WIFI_EVENT_STA_STOP)
		{
			powered = false; online = false; connected = false; current_address = 0;
			radio_log("wifi-driver-stopped"); return;
		}

		if(base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
		{
			current_address.store(0);
			connected = false;
			online = false; ++disconnects;
			const auto *event = static_cast<wifi_event_sta_disconnected_t *>(payload);
			last_reason = event->reason;
			radio_log("wifi-disconnected");
			ESP_LOGI("APP_WIFI", "Network %u disconnected, reason=%u; trying configured fallback",
			    unsigned(selected + 1), unsigned(event->reason));
			if(powered && !stopping) schedule_next();
		}

		if(base == IP_EVENT && id == IP_EVENT_STA_LOST_IP)
		{
			current_address.store(0); online = false; radio_log("wifi-ip-lost");
		}

		if(base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
		{
			const auto &info = *static_cast<ip_event_got_ip_t *>(payload);
			connected = true;
			online = true;
			failed_in_cycle = 0;
			(void)esp_timer_stop(retry_timer);
			current_address.store(info.ip_info.ip.addr);
			radio_log("wifi-connected");
			ESP_LOGI("APP_WIFI", "Network %u connected; Web UI: http://" IPSTR ":80/",
			    unsigned(selected + 1), IP2STR(&info.ip_info.ip));
		}
	}

}

esp_err_t wifi_init(bool enabled, uint32_t seconds)
{
	ESP_ERROR_CHECK(esp_netif_init());
	esp_netif_create_default_wifi_sta();
	wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&config));
	esp_timer_create_args_t timer{};
	timer.callback = +[](void *) {
			(void)esp_event_post(APP_WIFI_RETRY, 0, nullptr, 0, 0);
		};
	timer.name = "wifi_retry";
	ESP_ERROR_CHECK(esp_timer_create(&timer, &retry_timer));
	ESP_ERROR_CHECK(esp_event_handler_register(APP_WIFI_RETRY, ESP_EVENT_ANY_ID, network_event, nullptr));
	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, network_event, nullptr));
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, network_event, nullptr));
	ESP_LOGI("APP_WIFI", "%u configured networks; primary first, automatic fallback", unsigned(network_count()));
	// Both profiles are application-owned and never saved into SDK NVS.
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	configured_window = seconds;
	configured_enabled = enabled;
	window_seconds = seconds;
	deadline_seconds = seconds ? seconds_now() + seconds : 0;
	powered = enabled;
	if(enabled){ESP_ERROR_CHECK(esp_wifi_start()); ++starts;}

	radio_log(enabled ? "wifi-started" : "wifi-disabled-at-boot");
	return enabled ? esp_wifi_set_ps(WIFI_PS_NONE) : ESP_OK;
}

bool wifi_request(bool enabled, uint32_t seconds)
{
	if(seconds > 86400)return false;

	const Control command{enabled, seconds};
	return esp_event_post(APP_WIFI_RETRY, control_event, &command, sizeof(command), 0) == ESP_OK;
}

void wifi_poll(bool enabled, uint32_t seconds)
{
	if((seconds != configured_window || enabled != configured_enabled) && wifi_request(enabled, seconds))
	{
		configured_window = seconds; configured_enabled = enabled;
	}

	const auto deadline = deadline_seconds.load();
	if(powered && deadline && seconds_now() >= deadline && !expiry_queued.exchange(true))
	{
		if(esp_event_post(APP_WIFI_RETRY, expire_event, nullptr, 0, 0) != ESP_OK)expiry_queued = false;
	}
}

application::Value wifi_status()
{
	const auto deadline = deadline_seconds.load(), now = seconds_now();
	return application::Value::Object{
		{"enabled", powered.load()}, {"connected", online.load()}, {"stopping", stopping.load()},
		{"windowSeconds", window_seconds.load()},
		{"remainingSeconds",
		 !powered ? int64_t(0) : (deadline ? int64_t(deadline > now ? deadline - now : 0) : int64_t(-1))},
		{"attempts", attempts.load()}, {"disconnects", disconnects.load()},
		{"starts", starts.load()}, {"stops", stops.load()}, {"lastReason", last_reason.load()}};
}

void wifi_address(char (&address)[16])
{
	esp_ip4_addr_t ip{};
	ip.addr = current_address.load();
	if(ip.addr) std::snprintf(address, sizeof(address), IPSTR, IP2STR(&ip));
	else std::snprintf(address, sizeof(address), "offline");
}
