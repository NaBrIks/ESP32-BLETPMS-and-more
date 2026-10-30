#pragma once
#include "esp_err.h"
#include "modules/settings.hpp"

esp_err_t wifi_init(bool enabled,uint32_t window_seconds);
void wifi_poll(bool enabled,uint32_t window_seconds);
bool wifi_request(bool enabled, uint32_t seconds);
application::Value wifi_status();
void wifi_address(char (&address)[16]);
