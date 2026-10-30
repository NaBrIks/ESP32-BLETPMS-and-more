// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
#include "tpms.hpp"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <esp_timer.h>

namespace application
{
	namespace
	{
		std::atomic<int> requested_search{-1};
		std::string printed(uint32_t code)
		{
			char b[16];
			snprintf(b, sizeof(b), "%06lX", (unsigned long)code);
			return b;
		}

		const KnownSensor *registered(const Settings &s, uint32_t code)
		{
			for(auto &k : s.known)
				if(k.transport == "ble" && k.protocol == "tomtom" && k.code == code)
					return &k;

			return nullptr;
		}

	} // namespace
	void request_tpms_search(bool active)
	{
		requested_search.store(active ? 1 : 0);
	}

	void poll_tpms_search(State &s)
	{
		int request = requested_search.exchange(-1);
		if(request < 0)
			return;

		s.tpms_search = request != 0;
		++s.tpms_search_revision;
		s.tpms_search_started_ms = esp_timer_get_time() / 1000;
		for(auto &o : s.tpms_observations)
		{
			o.probe.reset();
			if(s.tpms_search)
				o.probe.sample(o.pressure_pa, o.last_ms);
		}
	}

	void observe_tomtom(State &s, const NaBTools::runtime::AdvertisementView &v,
	    const NaBTools::runtime::TomTomPacket &p)
	{
#if APP_TPMS_BLE_TOMTOM
		const auto code = printed_sensor_code(p.sensor_address());
		std::string key = "tpms.ble.";
		for(auto byte : v.address.bytes)
		{
			char b[3];
			snprintf(b, sizeof(b), "%02X", byte);
			key += b;
		}
		key += "." + std::to_string(v.address.type);
		auto existing =
		    std::find_if(s.tpms_observations.begin(), s.tpms_observations.end(),
		        [&](auto &o) {
			return o.discovery_id == key;
		});
		if(existing == s.tpms_observations.end())
		{
			if(s.tpms_observations.size() >= 96)
			{
				existing = s.tpms_observations.end();
				for(auto i = s.tpms_observations.begin(); i != s.tpms_observations.end();
				    ++i)
					if(!registered(s.settings, i->code) &&
					    (existing == s.tpms_observations.end() ||
					    i->last_ms < existing->last_ms))
						existing = i;

				if(existing == s.tpms_observations.end())
				{
					++s.tpms_discovery_dropped;
					return;
				}

				*existing = {};
			}
			else
			{
				s.tpms_observations.emplace_back();
				existing = s.tpms_observations.end() - 1;
			}

			existing->discovery_id = key;
		}

		auto &o = *existing;
		if(v.time_ms < o.last_ms)
			return;

		o.address = v.address.text();
		o.address_type = v.address.type;
		o.code = code;
		o.pressure_pa = p.pressure_pa();
		o.temperature_centi_c = p.temperature_centi_c();
		o.battery = p.battery();
		o.alarm = p.alarm();
		o.sensor_number = p.sensor_number();
		o.last_ms = v.time_ms;
		if(s.tpms_search && v.time_ms >= s.tpms_search_started_ms)
			o.probe.sample(o.pressure_pa, o.last_ms);

#else
		(void)s;
		(void)v;
		(void)p;
#endif
	}

	Value tpms_catalog(const State &s)
	{
		Value::Array drivers, devices;
		for(auto &d : tpms_drivers)
			drivers.push_back(Value::Object{{"id", std::string(d.id)},
					{"transport", std::string(transport_name(d.transport))},
					{"protocol", std::string(protocol_name(d.protocol))},
					{"label", std::string(d.label)}});
		auto entry = [&](const KnownSensor *k, const TpmsObservation *o) {
				const std::string id = k ? k->id : o->discovery_id;
				Value::Array tags;
				if(k)  for(auto &tag : k->tags)  tags.push_back(tag);

				Value::Object v{
					{"id", id},
					{"tags", tags},
					{"known", k != nullptr},
					{"observed", o != nullptr},
					{"driver", k ? k->driver : "tomtom-ble"},
					{"transport", k ? k->transport : "ble"},
					{"protocol", k ? k->protocol : "tomtom"},
					{"protocolId", k ? k->identity : printed(o->code)},
					{"code", k ? k->code : o->code},
					{"address", o ? o->address : (k ? k->transport_identity : "")},
					{"addressType", o ? int(o->address_type) : -1},
					{"lastSeenMs", o ? o->last_ms : 0},
					{"available", tpms_driver(k ? k->driver : "tomtom-ble") != nullptr}};
				v["pressurePa"] = o ? Value(o->pressure_pa) : Value{};
				v["pressureBar"] = o ? Value(double(o->pressure_pa) / 100000.) : Value{};
				v["temperatureC"] =
				    o ? Value(double(o->temperature_centi_c) / 100.) : Value{};
				v["sensorNumber"] = o ? Value(o->sensor_number) : Value{};
				v["changed"] = s.tpms_search && o && o->probe.changed;
				v["baselinePa"] = s.tpms_search && o && o->probe.initialized
				          ? Value(o->probe.baseline_pa)
				          : Value{};
				v["deltaPa"] = s.tpms_search && o && o->probe.initialized
				       ? Value(o->probe.delta_pa)
				       : Value{};
				v["peakDeltaPa"] = s.tpms_search && o && o->probe.initialized
				           ? Value(o->probe.peak_delta_pa)
				           : Value{};
				return Value(std::move(v));
			};
		for(auto &k : s.settings.known)
		{
			const TpmsObservation *o = nullptr;
			for(auto &v : s.tpms_observations)
				if(k.transport == "ble" && k.protocol == "tomtom" && v.code == k.code &&
				    (!o || v.last_ms > o->last_ms))
					o = &v;

			devices.push_back(entry(&k, o));
		}
		for(auto &o : s.tpms_observations)
			if(!registered(s.settings, o.code))
				devices.push_back(entry(nullptr, &o));

		return Value::Object{
			{"uptimeMs", uint64_t(esp_timer_get_time() / 1000)},
			{"drivers", drivers},
			{"devices", devices},
			{"search", Value::Object{{"active", s.tpms_search},
				 {"revision", s.tpms_search_revision},
				 {"startedMs", s.tpms_search_started_ms},
				 {"thresholdPa", 10000}}},
			{"dropped", s.tpms_discovery_dropped}};
	}

} // namespace application
