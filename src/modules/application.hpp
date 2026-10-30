// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
#pragma once
#include "sensor_config.hpp"
#include "settings.hpp"
#include "tpms_types.hpp"
#include <NaBTools/devices/tomtom/tpms/tpms.hpp>
#include <array>
#include <cstdint>
#include <lvgl_cbor/data_registry.hpp>
#include <string>

namespace application
{
	struct Sensor {
		bool present{};
		uint32_t code{};
		std::string id, position, identity, transport, protocol;
		NaBTools::runtime::TpmsReading reading;
		uint64_t last_seen_ms{}, journal_ms{};
		uint32_t advertisements{}, readings{};
		int last_rssi{};
	};

// Only the application/UI task owns this state. SDK and HTTP callbacks use
// fixed-size queues or owned transport snapshots instead of borrowing it.
	struct State {
		std::vector<Sensor> sensors;
		Settings settings;
		std::string ip = "offline";
		uint64_t main_tick{}, web_tick{};
		uint32_t reports{}, dropped{};
		std::string scan = "starting";
		std::string scan_mode;
		Value research, tomtom, wifi, ble;
		std::vector<TpmsObservation> tpms_observations;
		bool tpms_search{};
		uint32_t tpms_search_revision{}, tpms_discovery_dropped{};
		uint64_t tpms_search_started_ms{};
	};

	lvgl_cbor::DataValue tpms_message(const Sensor &sensor);
	void sync_settings(State &);
	std::vector<Sensor> active_wheels(const State &);
	void request_scan_mode(bool active);
} // namespace application
