// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
#pragma once
#include "modules/application.hpp"

void lcd_main();
void dashboard_init();
void dashboard_update(const application::State &state);
void dashboard_poll();
void dashboard_request(int page, bool capture);
bool dashboard_control(const std::string &);
application::Value dashboard_status();
application::Value dashboard_frame(size_t offset);
