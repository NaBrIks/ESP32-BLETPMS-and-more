// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
#pragma once
#include "esp_err.h"
#include "modules/application.hpp"

enum class WebNetwork : uint32_t { WifiStation = 1, WifiAp = 2, Ethernet = 4 };
esp_err_t web_init();
void web_network_enabled(WebNetwork network, bool enabled);
void web_request(bool enabled);
application::Value web_status();
void publish_web(const application::State &state);
application::Value web_probe();
