// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
#pragma once
using esp_err_t = int;
constexpr int ESP_OK = 0;
constexpr int ESP_ERR_NVS_NOT_FOUND = 1;
constexpr int ESP_ERR_NVS_INVALID_LENGTH = 2;
constexpr int ESP_FAIL = 3;
inline const char *esp_err_to_name(int)
{
	return "simulated_nvs_error";
}
