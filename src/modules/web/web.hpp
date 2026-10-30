// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
#pragma once
#include "esp_err.h"
#include "modules/application.hpp"

esp_err_t start_web_server();
void publish_web(const application::State &state);
