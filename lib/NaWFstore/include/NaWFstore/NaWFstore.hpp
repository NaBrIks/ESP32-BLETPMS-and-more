// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#ifndef NAWFSTORE_MAX_PROFILES
#define NAWFSTORE_MAX_PROFILES 8
#endif
#ifndef NAWFSTORE_WITH_NVS
#define NAWFSTORE_WITH_NVS 1
#endif
#ifndef NAWFSTORE_WITH_CONSOLE
#define NAWFSTORE_WITH_CONSOLE 1
#endif
#ifndef NAWFSTORE_WITH_WEB
#define NAWFSTORE_WITH_WEB 1
#endif
#ifndef NAWFSTORE_WITH_SOFTAP
#define NAWFSTORE_WITH_SOFTAP 1
#endif
#ifndef NAWFSTORE_WITH_BOOTSTRAP
#define NAWFSTORE_WITH_BOOTSTRAP 1
#endif
static_assert(NAWFSTORE_MAX_PROFILES >= 1 && NAWFSTORE_MAX_PROFILES <= 8);

namespace NaWFstore
{
	struct Profile
	{
		uint32_t id{};
		std::string ssid;
		std::string password;
		int16_t priority{};
		bool enabled{true};
	};

	struct Config
	{
		std::vector<Profile> profiles;
		bool startup_enabled{true};
		uint32_t startup_window_seconds{};
	};

	struct ApConfig
	{
		std::string ssid;
		std::string password;
		uint32_t seconds{300};
	};

	struct DriverStatus
	{
		bool station_started{};
		bool connected{};
		std::string ip;
		int rssi{};
		bool ap_active{};
		std::string ap_ip;
		uint32_t last_reason{};
		uint32_t disconnects{};
		uint32_t starts{};
		uint32_t stops{};
	};

	enum class LoadResult { missing, loaded, error };

	class IProfileStore
	{
	public:
		virtual ~IProfileStore() = default;
		virtual LoadResult load(Config &config, std::string &error) = 0;
		virtual bool save(const Config &config, std::string &error) = 0;
	};

	class IWifiDriver
	{
	public:
		virtual ~IWifiDriver() = default;
		virtual bool set_station(bool enabled, std::string &error) = 0;
		virtual bool connect(const Profile &profile, std::string &error) = 0;
		virtual bool disconnect(std::string &error) = 0;
		virtual bool start_ap(const ApConfig &config, std::string &error) = 0;
		virtual bool stop_ap(std::string &error) = 0;
		virtual DriverStatus snapshot() = 0;
	};

	class IClock
	{
	public:
		virtual ~IClock() = default;
		virtual uint64_t milliseconds() const = 0;
	};

	struct Status
	{
		DriverStatus driver;
		bool enabled{};
		bool ready{};
		uint32_t current_profile{};
		uint32_t attempts{};
		uint32_t remaining_seconds{}; // zero means unlimited when enabled
		uint32_t ap_remaining_seconds{};
		std::string error;
	};

	bool validate(const Config &config, std::string &error);

	// Single-owner API. Adapters must serialize calls and copy status for other tasks.
	class Manager
	{
	public:
		Manager(IProfileStore &store, IWifiDriver &driver, IClock &clock);
		bool initialize(const Config &bootstrap, std::string &error);
		bool put_profile(Profile profile, std::string &error);
		bool remove_profile(uint32_t id, std::string &error);
		bool set_startup(bool enabled, uint32_t seconds, std::string &error);
		bool enable(uint32_t seconds, std::string &error);
		bool disable(std::string &error);
		bool connect(uint32_t id, std::string &error);
		bool start_ap(const ApConfig &config, std::string &error);
		bool stop_ap(std::string &error);
		void poll();
		Status status();
		const Config &config() const;

	private:
		bool commit(const Config &next, std::string &error);
		bool begin_attempt(uint32_t id, std::string &error);
		void schedule_next();
		std::vector<uint32_t> ordered_profiles() const;
		IProfileStore &store_;
		IWifiDriver &driver_;
		IClock &clock_;
		Config config_;
		Status status_;
		uint64_t deadline_{}, ap_deadline_{}, attempt_deadline_{}, retry_at_{};
		unsigned cycle_attempts_{};
		bool was_connected_{};
	};
}
