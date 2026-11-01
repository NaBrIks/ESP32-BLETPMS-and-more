if(CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_register(PRIV_REQUIRES NaWFstore NaBTools NaCBORUIforLVGL NaWServer esp_lcd esp_wifi
        esp_event esp_netif nvs_flash esp_timer driver spiffs json espressif__cbor espressif__esp_lcd_touch espressif__esp_lcd_touch_gt911)
    return()
endif()
set(appearance_dir "${CMAKE_CURRENT_LIST_DIR}/../../NaCBORUIforLVGL/examples/common/appearance")
set(display_sources modules/lcd/dashboard.cpp ui/fonts/app_text14.c ui/fonts/app_text20.c "${appearance_dir}/appearance.cpp")
set(display_requirements)
if(APP_BOARD STREQUAL "lcd7")
    list(APPEND display_sources boards/lcd7/board.cpp boards/lcd7/adapter.cpp)
    list(APPEND display_requirements espressif__esp_lcd_touch espressif__esp_lcd_touch_gt911)
elseif(APP_BOARD STREQUAL "lilygo")
    list(APPEND display_sources modules/lcd/lcd_t_d_s3.cpp)
elseif(APP_BOARD STREQUAL "headless")
    set(display_sources boards/headless.cpp)
else()
    message(FATAL_ERROR "Unknown APP_BOARD")
endif()
idf_component_register(
    SRCS main.cpp modules/composition_root.cpp modules/settings.cpp modules/tpms.cpp modules/ble/ble.cpp
         modules/wifi/wifi.cpp modules/web/web.cpp modules/diagnostic/diagnostic.cpp ${display_sources}
    INCLUDE_DIRS . "${appearance_dir}"
    PRIV_REQUIRES NaWFstore NaBTools NaCBORUIforLVGL NaWServer esp_lcd esp_wifi
                  esp_event esp_netif nvs_flash esp_timer driver spiffs json espressif__cbor ${display_requirements}
    EMBED_TXTFILES www/index.html www/ble.html www/css.css
    EMBED_FILES ui/project.cbor ui/appearance.cbor)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_20)
target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_LCD7_TOUCH=1 LV_LVGL_H_INCLUDE_SIMPLE=1)
if(APP_BOARD STREQUAL "lcd7")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_DISPLAY_WIDTH=800 APP_DISPLAY_HEIGHT=480)
endif()
