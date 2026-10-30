// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <lvgl_cbor/data_registry.hpp>
#include <string>
#include <vector>

namespace application
{
	using Value = lvgl_cbor::DataValue;
	struct KnownSensor {
		std::string id, transport, protocol;
		uint32_t code{};
		std::string identity, driver, transport_identity;
		std::vector<std::string> tags;
	};
	struct Wheel {
		std::string name, sensor;
	};
	struct Axle {
		std::string name;
		std::vector<Wheel> wheels;
	};
	struct Vehicle {
		std::string id, name;
		std::vector<Axle> axles;
	};
	struct Settings {
		uint32_t revision{};
		bool wifi_enabled{true};
		uint32_t wifi_window_seconds{}; // zero: no automatic stop
		uint32_t ble_interval_ms{80}, ble_window_ms{80};
		std::string native_theme{"cream"};
		std::string active;
		std::vector<KnownSensor> known;
		std::vector<Vehicle> garage;
	};
	const Value &member(const Value &, const std::string &);
	std::string string_value(const Value &);
	int64_t integer_value(const Value &, int64_t fallback = 0);
	bool parse_json(const std::string &, Value &, std::string &);
	Value settings_value(const Settings &);
	bool settings_decode(const Value &, Settings &, std::string &);
	Settings default_settings();
// All FS I/O runs on a separate worker; callbacks/main loop only enqueue or
// copy.
	void storage_start();
	bool settings_copy(Settings &); // false when unchanged
	Value storage_status();
	Value settings_snapshot();
	uint32_t submit_discovered(uint32_t revision, KnownSensor sensor);
	uint32_t submit_command(std::string body); // zero: bounded queue full
	Value command_status(uint32_t id);
	void journal(std::string line); // bounded, non-blocking, no GAP callback use
	Value files_list();
	bool file_read(const std::string &name, size_t offset, std::string &out,
	    size_t &total);
} // namespace application
