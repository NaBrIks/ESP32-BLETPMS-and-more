// RGB wiring and frame ownership adapted from ESP32-yaidfws touch_lcd7.
// Adapted for the application. GPL-3.0; see LICENSE and PROVENANCE.md.
#include "board.hpp"
#include "lvgl.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#if APP_LCD7_TOUCH
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_gt911.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <cstdlib>
#include <algorithm>
namespace {
esp_lcd_panel_handle_t panel;
StaticSemaphore_t event_storage;SemaphoreHandle_t event;
uint32_t frames{},vsyncs{};
uint32_t refreshes{},rendered_areas{};
uint64_t rendered_pixels{};
int display_core{};
const uint16_t *last_frame{};
uint32_t touch_errors{};
bool touch_ready{};
#if APP_LCD7_TOUCH
esp_lcd_touch_handle_t touch_controller;
void touch_read(lv_indev_t *,lv_indev_data_t *data){
 const auto result=esp_lcd_touch_read_data(touch_controller);
 esp_lcd_touch_point_data_t point{};uint8_t count{};
 if(result!=ESP_OK||esp_lcd_touch_get_data(touch_controller,&point,&count,1)!=ESP_OK){
  __atomic_fetch_add(&touch_errors,1u,__ATOMIC_RELAXED);data->state=LV_INDEV_STATE_RELEASED;return;
 }
 data->state=count?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
 if(count){data->point.x=std::min<uint16_t>(point.x,799);data->point.y=std::min<uint16_t>(point.y,479);}
}
esp_err_t initialize_touch(lv_display_t *display){
 esp_lcd_panel_io_i2c_config_t io_config{};io_config.dev_addr=ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
 io_config.control_phase_bytes=1;io_config.lcd_cmd_bits=16;io_config.flags.disable_control_phase=1;
 esp_lcd_panel_io_handle_t io{};
 auto result=esp_lcd_new_panel_io_i2c(static_cast<esp_lcd_i2c_bus_handle_t>(I2C_NUM_0),&io_config,&io);
 if(result!=ESP_OK)return result;
 esp_lcd_touch_config_t config{};config.x_max=800;config.y_max=480;
 config.rst_gpio_num=GPIO_NUM_NC;config.int_gpio_num=GPIO_NUM_NC;
 result=esp_lcd_touch_new_i2c_gt911(io,&config,&touch_controller);
 if(result!=ESP_OK){esp_lcd_panel_io_del(io);return result;}
 auto *input=lv_indev_create();if(!input){esp_lcd_touch_del(touch_controller);touch_controller=nullptr;esp_lcd_panel_io_del(io);return ESP_ERR_NO_MEM;}
 lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_display(input,display);lv_indev_set_read_cb(input,touch_read);
 touch_ready=true;return ESP_OK;
}
#endif
uint32_t read(const uint32_t &value){return __atomic_load_n(&value,__ATOMIC_ACQUIRE);}
bool IRAM_ATTR frame(esp_lcd_panel_handle_t,const esp_lcd_rgb_panel_event_data_t*,void*){__atomic_fetch_add(&frames,1u,__ATOMIC_RELEASE);BaseType_t wake=pdFALSE;xSemaphoreGiveFromISR(event,&wake);return wake==pdTRUE;}
bool IRAM_ATTR vsync(esp_lcd_panel_handle_t,const esp_lcd_rgb_panel_event_data_t*,void*){__atomic_fetch_add(&vsyncs,1u,__ATOMIC_RELEASE);BaseType_t wake=pdFALSE;xSemaphoreGiveFromISR(event,&wake);return wake==pdTRUE;}
void wait(const uint32_t &counter,uint32_t before){const auto started=xTaskGetTickCount();while(read(counter)==before){if(xTaskGetTickCount()-started>pdMS_TO_TICKS(500))abort();xSemaphoreTake(event,pdMS_TO_TICKS(50));}}
void flush(lv_display_t *display,const lv_area_t *area,uint8_t *pixels){
 ++rendered_areas;
 rendered_pixels+=uint64_t(lv_area_get_width(area))*lv_area_get_height(area);
 // DIRECT renders dirty rectangles into a full-sized back buffer. Only publish
 // after the last rectangle: the panel must never scan a half-rendered frame.
 if(lv_display_flush_is_last(display)){
  const auto previous=read(frames);
  ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel,0,0,800,480,pixels));
  wait(frames,previous);wait(vsyncs,read(vsyncs));
  last_frame=reinterpret_cast<const uint16_t*>(pixels);++refreshes;
 }
 lv_display_flush_ready(display);
}
esp_err_t expander(uint8_t address,uint8_t value){return i2c_master_write_to_device(I2C_NUM_0,address,&value,1,pdMS_TO_TICKS(100));}
}
uint32_t display_frame_count(){return read(frames);}
DisplayRefreshStats display_refresh_stats(){return {refreshes,rendered_areas,rendered_pixels,display_core};}
uint16_t display_pixel(int32_t x,int32_t y){return last_frame&&x>=0&&x<800&&y>=0&&y<480?last_frame[y*800+x]:0;}
bool display_touch_available(){return touch_ready;}
uint32_t display_touch_errors(){return __atomic_load_n(&touch_errors,__ATOMIC_RELAXED);}
esp_err_t initialize_display(){
 display_core=xPortGetCoreID();
 i2c_config_t bus{};bus.mode=I2C_MODE_MASTER;bus.sda_io_num=8;bus.scl_io_num=9;bus.sda_pullup_en=GPIO_PULLUP_ENABLE;bus.scl_pullup_en=GPIO_PULLUP_ENABLE;bus.master.clk_speed=400000;
 ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0,&bus));ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0,I2C_MODE_MASTER,0,0,0));
 ESP_ERROR_CHECK(expander(0x24,0x01));ESP_ERROR_CHECK(gpio_set_direction(GPIO_NUM_4,GPIO_MODE_OUTPUT));ESP_ERROR_CHECK(expander(0x38,0x2c));vTaskDelay(pdMS_TO_TICKS(100));gpio_set_level(GPIO_NUM_4,0);vTaskDelay(pdMS_TO_TICKS(100));ESP_ERROR_CHECK(expander(0x38,0x2e));vTaskDelay(pdMS_TO_TICKS(200));ESP_ERROR_CHECK(gpio_set_direction(GPIO_NUM_4,GPIO_MODE_INPUT));
 esp_lcd_rgb_panel_config_t config{};config.clk_src=LCD_CLK_SRC_DEFAULT;config.timings.pclk_hz=12000000;config.timings.h_res=800;config.timings.v_res=480;
 config.timings.hsync_pulse_width=4;config.timings.hsync_back_porch=8;config.timings.hsync_front_porch=8;config.timings.vsync_pulse_width=4;config.timings.vsync_back_porch=8;config.timings.vsync_front_porch=8;config.timings.flags.pclk_active_neg=1;
 // Match the UI demo's sixteen-line bounce buffers. Their refill deadline is
 // twice that of eight lines, allowing more interrupt latency with Wi-Fi/BLE.
 // https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html#bounce-buffer-with-single-psram-frame-buffer
 config.data_width=16;config.bits_per_pixel=16;config.num_fbs=2;config.bounce_buffer_size_px=800*display_bounce_rows;config.dma_burst_size=64;
 config.hsync_gpio_num=46;config.vsync_gpio_num=3;config.de_gpio_num=5;config.pclk_gpio_num=7;config.disp_gpio_num=-1;
 const int pins[]{14,38,18,17,10,39,0,45,48,47,21,1,2,42,41,40};std::copy(pins,pins+16,config.data_gpio_nums);config.flags.fb_in_psram=1;
 ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&config,&panel));ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
 event=xSemaphoreCreateBinaryStatic(&event_storage);if(!event)return ESP_ERR_NO_MEM;
 esp_lcd_rgb_panel_event_callbacks_t callbacks{};callbacks.on_frame_buf_complete=frame;callbacks.on_vsync=vsync;ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel,&callbacks,nullptr));
 void *front=nullptr,*back=nullptr;ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel,2,&front,&back));
 lv_init();auto *display=lv_display_create(800,480);if(!display)return ESP_ERR_NO_MEM;
 lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(display,back,front,800*480*2,LV_DISPLAY_RENDER_MODE_DIRECT);lv_display_set_flush_cb(display,flush);
 ESP_LOGI("APP_LCD","RGB DIRECT, 2 buffers, %lu bounce rows, interrupt core %d",(unsigned long)display_bounce_rows,display_core);
#if APP_LCD7_TOUCH
 const auto touch_result=initialize_touch(display);
 if(touch_result!=ESP_OK)ESP_LOGW("style_lab","GT911 unavailable: %s",esp_err_to_name(touch_result));
#endif
 return expander(0x38,0x1e);
}
