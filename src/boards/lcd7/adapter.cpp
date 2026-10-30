#include "board.hpp"
#include <lvgl.h>
#include <esp_timer.h>

void lcd_main()
{
    ESP_ERROR_CHECK(initialize_display());
    lv_tick_set_cb([]() -> uint32_t { return esp_timer_get_time() / 1000; });
}
