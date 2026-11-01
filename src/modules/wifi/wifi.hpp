// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
#pragma once
#include "esp_err.h"
#include "modules/settings.hpp"
esp_err_t wifi_init(bool enabled, uint32_t window_seconds);
void wifi_poll(bool enabled, uint32_t window_seconds);
bool wifi_request(bool enabled, uint32_t seconds);
bool wifi_ap_request(bool enabled, uint32_t seconds = 300);
application::Value wifi_status();
application::Value wifi_diagnostics();
application::Value wifi_profiles();
application::Value wifi_config_command(const application::Value &command);
application::Value wifi_operation(uint32_t id);
std::string wifi_ap_credentials();
void wifi_address(char (&address)[16]);
