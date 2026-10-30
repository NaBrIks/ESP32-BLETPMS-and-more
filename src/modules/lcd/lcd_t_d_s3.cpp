// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
#include "dashboard.hpp"
#include <lvgl.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_ops.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static esp_lcd_panel_handle_t panel;

static bool flush_complete(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *context)
{
	lv_display_flush_ready(static_cast<lv_display_t *>(context));
	return false;
}

static void flush(lv_display_t *, const lv_area_t *area, uint8_t *pixels)
{
	// LVGL releases the buffer only after DMA completes; it must not be reused
	// immediately after esp_lcd_panel_draw_bitmap queues the transfer.
	ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1,
	    area->x2 + 1, area->y2 + 1, pixels));
}

void lcd_main()
{
	constexpr int width = 320, height = 170;
	gpio_config_t output{};
	output.pin_bit_mask = (1ULL << 38) | (1ULL << 9) | (1ULL << 15);
	output.mode = GPIO_MODE_OUTPUT;
	ESP_ERROR_CHECK(gpio_config(&output));
	gpio_set_level(GPIO_NUM_15, 1);
	gpio_set_level(GPIO_NUM_9, 1);
	gpio_set_level(GPIO_NUM_38, 1);

	lv_init();
	lv_tick_set_cb([]() -> uint32_t {
		return esp_timer_get_time() / 1000;
	});
	auto *display = lv_display_create(width, height);
	lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
	lv_display_set_flush_cb(display, flush);
	lv_display_set_default(display);

	esp_lcd_i80_bus_config_t bus_config{};
	bus_config.dc_gpio_num = 7;
	bus_config.wr_gpio_num = 8;
	bus_config.clk_src = LCD_CLK_SRC_DEFAULT;
	const int pins[] = {39, 40, 41, 42, 45, 46, 47, 48};
	for(size_t index = 0; index < 8; ++index)
		bus_config.data_gpio_nums[index] = pins[index];
	bus_config.bus_width = 8;
	constexpr size_t buffer_bytes = width * 20 * 2;
	bus_config.max_transfer_bytes = buffer_bytes;
	esp_lcd_i80_bus_handle_t bus;
	ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_config, &bus));

	esp_lcd_panel_io_i80_config_t io_config{};
	io_config.cs_gpio_num = 6;
	io_config.pclk_hz = 6528000;
	io_config.trans_queue_depth = 4;
	io_config.lcd_cmd_bits = 8;
	io_config.lcd_param_bits = 8;
	io_config.dc_levels.dc_data_level = 1;
	io_config.flags.swap_color_bytes = 1;
	io_config.on_color_trans_done = flush_complete;
	io_config.user_ctx = display;
	esp_lcd_panel_io_handle_t io;
	ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(bus, &io_config, &io));
	esp_lcd_panel_dev_config_t panel_config{};
	panel_config.reset_gpio_num = 5;
	panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
	panel_config.bits_per_pixel = 16;
	ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel_config, &panel));
	ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
	ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
	ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, true));
	ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, true, false));
	ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
	ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel, 0, 35));
	ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

	// Retain the existing board-specific ST7789 porch/power/gamma sequence.
	const struct { uint8_t command; uint8_t data[14]; uint8_t length; } commands[] = {
		{0x3A, {0x05}, 1}, {0xB2, {0x0B, 0x0B, 0, 0x33, 0x33}, 5},
		{0xB7, {0x75}, 1}, {0xBB, {0x28}, 1}, {0xC0, {0x2C}, 1},
		{0xC2, {0x01}, 1}, {0xC3, {0x1F}, 1}, {0xC6, {0x13}, 1},
		{0xD0, {0xA7}, 1}, {0xD0, {0xA4, 0xA1}, 2}, {0xD6, {0xA1}, 1},
		{0xE0, {0xF0, 5, 0x0A, 6, 6, 3, 0x2B, 0x32, 0x43, 0x36, 0x11, 0x10, 0x2B, 0x32}, 14},
		{0xE1, {0xF0, 8, 0x0C, 0x0B, 9, 0x24, 0x2B, 0x22, 0x43, 0x38, 0x15, 0x16, 0x2F, 0x37}, 14}};
	for(const auto &command : commands)
		ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, command.command, command.data, command.length));

	auto *first = heap_caps_malloc(buffer_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
	auto *second = heap_caps_malloc(buffer_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
	ESP_ERROR_CHECK(first && second ? ESP_OK : ESP_ERR_NO_MEM);
	lv_display_set_buffers(display, first, second, buffer_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
}
