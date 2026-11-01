// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
#include <NaWFstore/EspIdfWifiDriver.hpp>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <cstdio>
#include <cstring>
namespace NaWFstore
{
	uint64_t EspClock::milliseconds() const
	{
		return esp_timer_get_time() / 1000;
	}

	static bool checked(esp_err_t result, std::string &error)
	{
		if(result == ESP_OK) return true;

		error = esp_err_to_name(result);
		return false;
	}

	bool EspIdfWifiDriver::initialize(std::string &error)
	{
		if(!checked(esp_netif_init(), error)) return false;

		if(!esp_netif_create_default_wifi_sta()) { error = "station_netif"; return false; }

#if NAWFSTORE_WITH_SOFTAP
		ap_netif_ = esp_netif_create_default_wifi_ap();
		if(!ap_netif_) { error = "ap_netif"; return false; }

#endif
		wifi_init_config_t configuration = WIFI_INIT_CONFIG_DEFAULT();
		if(!checked(esp_wifi_init(&configuration), error)) return false;

		if(!checked(esp_wifi_set_storage(WIFI_STORAGE_RAM), error)) return false;

		if(!checked(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event, this), error)) return false;

		return checked(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, event, this), error);
	}

	void EspIdfWifiDriver::event(void *context, esp_event_base_t base, int32_t id, void *data)
	{
		auto &self = *static_cast<EspIdfWifiDriver *>(context);
		std::lock_guard<std::mutex> lock(self.mutex_);
		if(base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
		{
			self.state_.connected = false;
			++self.state_.disconnects;
			self.state_.ip.clear();
			self.state_.last_reason = static_cast<wifi_event_sta_disconnected_t *>(data)->reason;
		}

		if(base == IP_EVENT && id == IP_EVENT_STA_GOT_IP && self.state_.station_started)
		{
			wifi_ap_record_t associated{};
			if(esp_wifi_sta_get_ap_info(&associated) != ESP_OK || self.expected_ssid_ !=
			    std::string(reinterpret_cast<const char *>(associated.ssid),
			    strnlen(reinterpret_cast<const char *>(associated.ssid), 32))) return;

			char address[16];
			const auto &info = *static_cast<ip_event_got_ip_t *>(data);
			std::snprintf(address, sizeof(address), IPSTR, IP2STR(&info.ip_info.ip));
			self.state_.ip = address;
			self.state_.connected = true;
		}

		if(base == IP_EVENT && id == IP_EVENT_STA_LOST_IP)
		{
			self.state_.connected = false;
			self.state_.ip.clear();
		}
	}

	bool EspIdfWifiDriver::apply_mode(bool station, bool ap, std::string &error)
	{
		const bool was_running = running_;
		if(!station && !ap)
		{
			if(running_ && !checked(esp_wifi_stop(), error)) return false;

			running_ = false;
		}
		else
		{
			const auto mode = station ? (ap ? WIFI_MODE_APSTA : WIFI_MODE_STA) : WIFI_MODE_AP;
			if(!checked(esp_wifi_set_mode(mode), error)) return false;

			if(!running_ && !checked(esp_wifi_start(), error)) return false;

			running_ = true;
			(void)esp_wifi_set_ps(WIFI_PS_NONE);
		}

		std::lock_guard<std::mutex> lock(mutex_);
		if(!was_running && running_) ++state_.starts;

		if(was_running && !running_) ++state_.stops;

		state_.station_started = station;
		state_.ap_active = ap;
		if(!station) { state_.connected = false; state_.ip.clear(); }

		if(!ap) state_.ap_ip.clear();

		return true;
	}

	bool EspIdfWifiDriver::set_station(bool enabled, std::string &error)
	{
		return apply_mode(enabled, snapshot().ap_active, error);
	}

	bool EspIdfWifiDriver::connect(const Profile &profile, std::string &error)
	{
		wifi_config_t configuration{};
		if(profile.ssid.empty() || profile.ssid.size() > sizeof(configuration.sta.ssid) ||
		    profile.password.size() > sizeof(configuration.sta.password)) { error = "profile_length"; return false; }

		{
			std::lock_guard<std::mutex> lock(mutex_);
			expected_ssid_ = profile.ssid;
			state_.connected = false;
			state_.ip.clear();
		}
		(void)esp_wifi_disconnect();
		std::memcpy(configuration.sta.ssid, profile.ssid.data(), profile.ssid.size());
		std::memcpy(configuration.sta.password, profile.password.data(), profile.password.size());
		configuration.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
		if(!checked(esp_wifi_set_config(WIFI_IF_STA, &configuration), error)) return false;

		return checked(esp_wifi_connect(), error);
	}

	bool EspIdfWifiDriver::disconnect(std::string &error)
	{
		const auto result = esp_wifi_disconnect();
		{
			std::lock_guard<std::mutex> lock(mutex_);
			state_.connected = false;
			state_.ip.clear();
		}
		return result == ESP_ERR_WIFI_NOT_STARTED || result == ESP_ERR_WIFI_NOT_CONNECT || checked(result, error);
	}

	bool EspIdfWifiDriver::start_ap(const ApConfig &config, std::string &error)
	{
#if NAWFSTORE_WITH_SOFTAP
		if(config.ssid.empty() || config.ssid.size() > 32 || config.password.size() < 8 || config.password.size() > 63)
		{ error = "ap_configuration"; return false; }

		const bool station = snapshot().station_started;
		if(!checked(esp_wifi_set_mode(station ? WIFI_MODE_APSTA : WIFI_MODE_AP), error)) return false;

		wifi_config_t configuration{};
		std::memcpy(configuration.ap.ssid, config.ssid.data(), config.ssid.size());
		configuration.ap.ssid_len = config.ssid.size();
		std::memcpy(configuration.ap.password, config.password.data(), config.password.size());
		configuration.ap.authmode = WIFI_AUTH_WPA2_PSK;
		configuration.ap.max_connection = 4;
		configuration.ap.channel = 1;
		if(!checked(esp_wifi_set_config(WIFI_IF_AP, &configuration), error)) return false;

		if(!apply_mode(station, true, error)) return false;

		esp_netif_ip_info_t info{};
		if(!checked(esp_netif_get_ip_info(ap_netif_, &info), error)) return false;

		char address[16];
		std::snprintf(address, sizeof(address), IPSTR, IP2STR(&info.ip));
		std::lock_guard<std::mutex> lock(mutex_);
		state_.ap_ip = address;
		return true;
#else
		error = "softap_disabled";
		return false;
#endif
	}

	bool EspIdfWifiDriver::stop_ap(std::string &error)
	{
		return apply_mode(snapshot().station_started, false, error);
	}

	DriverStatus EspIdfWifiDriver::snapshot()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if(state_.connected)
		{
			wifi_ap_record_t record{};
			if(esp_wifi_sta_get_ap_info(&record) == ESP_OK) state_.rssi = record.rssi;
		}

		return state_;
	}

}
