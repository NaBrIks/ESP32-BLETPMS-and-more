// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
#include "NaWFstore/NaWFstore.hpp"
#include <cassert>
#include <vector>
using namespace NaWFstore;

struct Store : IProfileStore
{
	LoadResult result{LoadResult::missing};
	Config persisted;
	bool fail_save{};
	unsigned writes{};

	LoadResult load(Config &out, std::string &) override
	{
		out = persisted;
		return result;
	}

	bool save(const Config &value, std::string &error) override
	{
		++writes;
		if(fail_save) { error = "simulated_failure"; return false; }

		persisted = value;
		return true;
	}

};

struct Clock : IClock
{
	uint64_t now{};

	uint64_t milliseconds() const override
	{
		return now;
	}

};

struct Driver : IWifiDriver
{
	DriverStatus link;
	std::vector<uint32_t> attempts;

	bool set_station(bool on, std::string &) override
	{
		link.station_started = on;
		if(!on) link.connected = false;

		return true;
	}

	bool connect(const Profile &profile, std::string &) override
	{
		attempts.push_back(profile.id);
		return true;
	}

	bool disconnect(std::string &) override
	{
		link.connected = false;
		return true;
	}

	bool start_ap(const ApConfig &, std::string &) override
	{
		link.ap_active = true;
		return true;
	}

	bool stop_ap(std::string &) override
	{
		link.ap_active = false;
		return true;
	}

	DriverStatus snapshot() override
	{
		return link;
	}

};

int main()
{
	std::string error;
	Config initial;
	initial.profiles = {{1, "primary", "password1", 5, true}};
#if NAWFSTORE_MAX_PROFILES > 1
	initial.profiles.push_back({2, "fallback", "password2", 1, true});
#endif
	assert(validate(initial, error));
	Config bad = initial;
	bad.profiles[0].password = "short";
	assert(!validate(bad, error));

	Store store;
	Clock clock;
	Driver driver;
	Manager manager(store, driver, clock);
	assert(manager.initialize(initial, error));
	assert(store.writes == 1);
	manager.poll();
	assert(driver.attempts == std::vector<uint32_t>{1});
	clock.now = 19999;
	manager.poll();
	assert(driver.attempts.size() == 1);
	clock.now = 20000;
	manager.poll();
	clock.now += 250;
	manager.poll();
#if NAWFSTORE_MAX_PROFILES > 1
	assert(driver.attempts == (std::vector<uint32_t>{1, 2}));
	clock.now += 20000;
	manager.poll();
	clock.now += 14999;
	manager.poll();
	assert(driver.attempts.size() == 2);
	clock.now += 1;
	manager.poll();
	assert(driver.attempts == (std::vector<uint32_t>{1, 2, 1}));
#endif
	// A live link is retained and periodic polls do not roam.
	driver.link.connected = true;
	manager.poll();
	const auto attempts = driver.attempts.size();
	clock.now += 60000;
	manager.poll();
	assert(driver.attempts.size() == attempts);
	// Failed persistence cannot publish a changed startup policy.
	store.fail_save = true;
	assert(!manager.set_startup(false, 0, error));
	assert(manager.config().startup_enabled);
	store.fail_save = false;
	assert(manager.start_ap({"setup", "password", 1}, error));
	clock.now += 1000;
	manager.poll();
	assert(!driver.link.ap_active);
	assert(manager.enable(1, error));
	clock.now += 1000;
	manager.poll();
	assert(!manager.status().enabled);
	const auto after_stop = driver.attempts.size();
	driver.link.connected = false; // delayed disconnect must not enable the radio
	clock.now += 60000;
	manager.poll();
	assert(driver.attempts.size() == after_stop);

	// Explicit saved empty configuration is never repopulated from bootstrap.
	Store empty;
	empty.result = LoadResult::loaded;
	empty.persisted.startup_enabled = false;
	Driver another_driver;
	Manager another(empty, another_driver, clock);
	assert(another.initialize(initial, error));
	assert(another.config().profiles.empty());
	assert(empty.writes == 0);
	assert(!another.status().enabled);

	// Invalid storage must remain untouched, even when bootstrap is supplied.
	Store broken;
	broken.result = LoadResult::error;
	Manager failed(broken, another_driver, clock);
	assert(!failed.initialize(initial, error));
	assert(broken.writes == 0);
	assert(!failed.status().ready);
	return 0;
}
