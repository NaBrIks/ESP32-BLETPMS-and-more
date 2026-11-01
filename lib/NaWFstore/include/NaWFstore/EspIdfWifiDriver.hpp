// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
#pragma once
#include "NaWFstore.hpp"
#include <esp_event.h>
#include <esp_netif.h>
#include <mutex>
namespace NaWFstore
{
	class EspClock final : public IClock
	{
	public:
		uint64_t milliseconds() const override;
	};

	class EspIdfWifiDriver final : public IWifiDriver
	{
	public:
		bool initialize(std::string &error);
		bool set_station(bool enabled, std::string &error) override;
		bool connect(const Profile &profile, std::string &error) override;
		bool disconnect(std::string &error) override;
		bool start_ap(const ApConfig &config, std::string &error) override;
		bool stop_ap(std::string &error) override;
		DriverStatus snapshot() override;
	private:
		static void event(void *context, esp_event_base_t base, int32_t id, void *data);
		bool apply_mode(bool station, bool ap, std::string &error);
		std::mutex mutex_;
		DriverStatus state_;
		std::string expected_ssid_;
		bool running_{};
		esp_netif_t *ap_netif_{};
	};
}
