// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <array>
#include <cstdint>

namespace application
{
	struct ConfiguredSensor
	{
		uint32_t code;
		const char *label;
		const char *short_label;
	};

// Device stickers print the three address bytes in wire/display order. The
// library retains its numeric little-endian field; normalize only at this app boundary.
	constexpr uint32_t printed_sensor_code(uint32_t raw)
	{
		return ((raw & 0xff) << 16) | (raw & 0xff00) | ((raw >> 16) & 0xff);
	}

#if __has_include("static_sensors_secret.hpp")
#include "static_sensors_secret.hpp"
#else
	inline constexpr const char *sensor_set = "UNASSIGNED";
	inline constexpr std::array<ConfiguredSensor, 4> configured_sensors{{
		{0, "Sensor 1", "1"}, {0, "Sensor 2", "2"}, {0, "Sensor 3", "3"}, {0, "Sensor 4", "4"}}};
#endif

	constexpr size_t configured_sensor_slot(uint32_t code)
	{
		for(size_t index = 0; index < configured_sensors.size(); ++index)
			if(configured_sensors[index].code && configured_sensors[index].code == code) return index;

		return configured_sensors.size();
	}

	static_assert(printed_sensor_code(0x563412) == 0x123456);
}
