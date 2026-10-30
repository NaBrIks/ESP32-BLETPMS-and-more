// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
#pragma once
#include "esp_err.h"
#include <cstdint>
inline constexpr uint32_t display_bounce_rows = 16;
esp_err_t initialize_display();
// Read only from the same LVGL task, after synchronous refresh.
uint32_t display_frame_count();
struct DisplayRefreshStats { uint32_t refreshes, areas; uint64_t pixels; int core; };
DisplayRefreshStats display_refresh_stats();
uint16_t display_pixel(int32_t x, int32_t y);
bool display_touch_available();
uint32_t display_touch_errors();
