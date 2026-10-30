#pragma once
#include "esp_err.h"
#include "modules/application.hpp"

esp_err_t start_web_server();
void publish_web(const application::State &state);
