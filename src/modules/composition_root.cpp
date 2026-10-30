// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
#include "composition_root.hpp"
#include "application.hpp"
#include "ble/ble.hpp"
#include "lcd/dashboard.hpp"
#include "wifi/wifi.hpp"
#include "web/web.hpp"
#include <esp_event.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_netif_sntp.h>
#include <cstdlib>
#include <ctime>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void init_main()
{
	static application::State state;
	// Preserve existing NVS: schema migration and persistent product settings
	// are intentionally outside this library-integration stage.
	ESP_ERROR_CHECK(nvs_flash_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	application::storage_start();
	application::sync_settings(state);
	lcd_main();
	dashboard_init();
	ESP_ERROR_CHECK(wifi_init(state.settings.wifi_enabled, state.settings.wifi_window_seconds));
	setenv("TZ", "YEKT-5", 1);
	tzset();
	esp_sntp_config_t clock_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
	(void)esp_netif_sntp_init(&clock_config);
	application::start_bluetooth(state);
	ESP_ERROR_CHECK(start_web_server());
	uint64_t next_web = 0, next_main = 0, next_ui = 0;
	for(;;)
	{
		application::poll_bluetooth(state);
		application::sync_settings(state);
		wifi_poll(state.settings.wifi_enabled, state.settings.wifi_window_seconds);
		char address[16];
		wifi_address(address);
		state.ip = address;
		const uint64_t now = esp_timer_get_time() / 1000;
		if(now >= next_main) { ++state.main_tick; next_main = now + 5000; }

		if(now >= next_ui) { dashboard_update(state); next_ui = now + 250; }

		if(now >= next_web) { state.wifi = wifi_status(); ++state.web_tick; publish_web(state); next_web = now + 1000; }

		// BT, DataRegistry and LVGL run in one serialized owner context.
		dashboard_poll();
		vTaskDelay(pdMS_TO_TICKS(10));
	}
}
