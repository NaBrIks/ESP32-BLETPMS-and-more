// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
#include "NaWFstore/NaWFstore.hpp"
#include <algorithm>
#include <limits>
#include <set>

namespace NaWFstore
{
	bool validate(const Config &config, std::string &error)
	{
		if(config.profiles.size() > NAWFSTORE_MAX_PROFILES || config.startup_window_seconds > 86400)
		{
			error = "configuration_limit";
			return false;
		}

		std::set<uint32_t> ids;
		for(const auto &profile : config.profiles)
		{
			const auto size = profile.password.size();
			if(!profile.id || !ids.insert(profile.id).second || profile.ssid.empty() || profile.ssid.size() > 32 ||
			    profile.ssid.find('\0') != std::string::npos || profile.password.find('\0') != std::string::npos ||
			    (size != 0 && (size < 8 || size > 64)))
			{
				error = "invalid_profile";
				return false;
			}

			if(size == 64 && !std::all_of(profile.password.begin(), profile.password.end(), [](unsigned char c) {
				return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
			}))
			{
				error = "invalid_psk";
				return false;
			}
		}
		error.clear();
		return true;
	}

	Manager::Manager(IProfileStore &store, IWifiDriver &driver, IClock &clock)
	  : store_(store), driver_(driver), clock_(clock)
	{
	}

	bool Manager::initialize(const Config &bootstrap, std::string &error)
	{
		if(status_.ready)
		{
			error = "already_initialized";
			return false;
		}

		Config loaded;
		const auto result = store_.load(loaded, error);
		if(result == LoadResult::error) return false;

		if(result == LoadResult::missing)
		{
#if NAWFSTORE_WITH_BOOTSTRAP
			loaded = bootstrap;
#else
			(void)bootstrap;
#endif
			if(!validate(loaded, error) || !store_.save(loaded, error)) return false;
		}

		if(!validate(loaded, error)) return false;

		config_ = loaded;
		status_.ready = true;
		return !config_.startup_enabled || enable(config_.startup_window_seconds, error);
	}

	const Config &Manager::config() const
	{
		return config_;
	}

	bool Manager::commit(const Config &next, std::string &error)
	{
		if(!status_.ready)
		{
			error = "not_initialized";
			return false;
		}

		if(!validate(next, error) || !store_.save(next, error)) return false;

		config_ = next;
		return true;
	}

	bool Manager::put_profile(Profile profile, std::string &error)
	{
		Config next = config_;
		if(!profile.id)
		{
			profile.id = 1;
			for(const auto &existing : next.profiles)
			{
				if(existing.id == std::numeric_limits<uint32_t>::max())
				{
					error = "id_exhausted";
					return false;
				}

				profile.id = std::max(profile.id, existing.id + 1);
			}
		}

		auto found = std::find_if(next.profiles.begin(), next.profiles.end(), [&](const Profile &p) {
			return p.id == profile.id;
		});
		if(found == next.profiles.end()) next.profiles.push_back(profile);
		else *found = profile;

		if(!commit(next, error)) return false;

		// An explicit edit is applied on the next reconnect, preserving a working link.
		if(status_.enabled && !driver_.snapshot().connected && !attempt_deadline_) retry_at_ = clock_.milliseconds();

		return true;
	}

	bool Manager::remove_profile(uint32_t id, std::string &error)
	{
		Config next = config_;
		auto found = std::find_if(next.profiles.begin(), next.profiles.end(), [id](const Profile &p) {
			return p.id == id;
		});
		if(found == next.profiles.end())
		{
			error = "profile_not_found";
			return false;
		}

		next.profiles.erase(found);
		if(!commit(next, error)) return false;

		if(status_.current_profile == id)
		{
			(void)driver_.disconnect(error);
			status_.current_profile = 0;
			attempt_deadline_ = 0;
			was_connected_ = false;
			retry_at_ = clock_.milliseconds();
		}

		return true;
	}

	bool Manager::set_startup(bool enabled, uint32_t seconds, std::string &error)
	{
		Config next = config_;
		next.startup_enabled = enabled;
		next.startup_window_seconds = seconds;
		return commit(next, error);
	}

	bool Manager::enable(uint32_t seconds, std::string &error)
	{
		if(!status_.ready || seconds > 86400)
		{
			error = "invalid_enable";
			return false;
		}

		if(!driver_.set_station(true, error)) return false;

		status_.enabled = true;
		deadline_ = seconds ? clock_.milliseconds() + uint64_t(seconds) * 1000 : 0;
		if(!attempt_deadline_ && !driver_.snapshot().connected) retry_at_ = clock_.milliseconds();

		return true;
	}

	bool Manager::disable(std::string &error)
	{
		status_.enabled = false;
		status_.current_profile = 0;
		deadline_ = ap_deadline_ = attempt_deadline_ = retry_at_ = 0;
		was_connected_ = false;
		std::string ap_error;
		const bool ap_ok = driver_.stop_ap(ap_error);
		const bool sta_ok = driver_.set_station(false, error);
		if(!ap_ok && sta_ok) error = ap_error;

		return ap_ok && sta_ok;
	}

	std::vector<uint32_t> Manager::ordered_profiles() const
	{
		std::vector<Profile> sorted;
		for(const auto &p : config_.profiles)
			if(p.enabled) sorted.push_back(p);

		std::stable_sort(sorted.begin(), sorted.end(), [](const Profile &a, const Profile &b) {
			return a.priority > b.priority;
		});
		std::vector<uint32_t> ids;
		for(const auto &p : sorted)
			ids.push_back(p.id);
		return ids;
	}

	bool Manager::begin_attempt(uint32_t id, std::string &error)
	{
		const auto found = std::find_if(config_.profiles.begin(), config_.profiles.end(), [id](const Profile &p) {
			return p.id == id && p.enabled;
		});
		if(found == config_.profiles.end())
		{
			error = "profile_not_found";
			return false;
		}

		status_.current_profile = id;
		++status_.attempts;
		attempt_deadline_ = clock_.milliseconds() + 20000;
		return driver_.connect(*found, error);
	}

	void Manager::schedule_next()
	{
		const auto count = ordered_profiles().size();
		const bool full_cycle = ++cycle_attempts_ >= count;
		if(full_cycle) cycle_attempts_ = 0;

		attempt_deadline_ = 0;
		retry_at_ = clock_.milliseconds() + (full_cycle ? 15000 : 250);
	}

	bool Manager::connect(uint32_t id, std::string &error)
	{
		const auto ids = ordered_profiles();
		if(std::find(ids.begin(), ids.end(), id) == ids.end())
		{
			error = "profile_not_found";
			return false;
		}

		if(!enable(0, error) || !driver_.disconnect(error)) return false;

		was_connected_ = false;
		cycle_attempts_ = 0;
		if(begin_attempt(id, error)) return true;

		schedule_next();
		return false;
	}

	bool Manager::start_ap(const ApConfig &config, std::string &error)
	{
#if NAWFSTORE_WITH_SOFTAP
		if(!status_.ready || config.ssid.empty() || config.ssid.size() > 32 || config.password.size() < 8 ||
		    config.password.size() > 63 || config.seconds > 86400 || config.ssid.find('\0') != std::string::npos ||
		    config.password.find('\0') != std::string::npos)
		{
			error = "invalid_ap_configuration";
			return false;
		}

		if(!driver_.start_ap(config, error)) return false;

		ap_deadline_ = config.seconds ? clock_.milliseconds() + uint64_t(config.seconds) * 1000 : 0;
		return true;
#else
		(void)config;
		error = "ap_not_built";
		return false;
#endif
	}

	bool Manager::stop_ap(std::string &error)
	{
		if(!driver_.stop_ap(error)) return false;

		ap_deadline_ = 0;
		return true;
	}

	void Manager::poll()
	{
		if(!status_.ready) return;

		const auto now = clock_.milliseconds();
		if(ap_deadline_ && now >= ap_deadline_) (void)stop_ap(status_.error);

		if(deadline_ && now >= deadline_) (void)disable(status_.error);

		if(!status_.enabled) return;

		const auto link = driver_.snapshot();
		if(link.connected)
		{
			was_connected_ = true;
			cycle_attempts_ = 0;
			attempt_deadline_ = 0;
			return;
		}

		if(was_connected_)
		{
			was_connected_ = false;
			schedule_next();
		}

		if(attempt_deadline_)
		{
			if(now < attempt_deadline_) return;

			(void)driver_.disconnect(status_.error);
			schedule_next();
			return;
		}

		if(now < retry_at_) return;

		const auto ids = ordered_profiles();
		if(ids.empty()) return;

		auto found = std::find(ids.begin(), ids.end(), status_.current_profile);
		const auto next = found == ids.end() || ++found == ids.end() ? ids.front() : *found;
		if(!begin_attempt(next, status_.error)) schedule_next();
	}

	Status Manager::status()
	{
		status_.driver = driver_.snapshot();
		const auto now = clock_.milliseconds();
		status_.remaining_seconds = deadline_ > now ? uint32_t((deadline_ - now + 999) / 1000) : 0;
		status_.ap_remaining_seconds = ap_deadline_ > now ? uint32_t((ap_deadline_ - now + 999) / 1000) : 0;
		return status_;
	}

}
