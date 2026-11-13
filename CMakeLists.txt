cmake_minimum_required(VERSION 3.16)
set(APP_BOARD "lcd7" CACHE STRING "lcd7, lilygo or headless")
if(APP_BOARD STREQUAL "lcd7" OR APP_BOARD STREQUAL "lilygo")
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.s3.defaults")
endif()
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/lib/NaWFstore"
    "${CMAKE_CURRENT_LIST_DIR}/src")
set(LVGL_CBOR_PROJECT_SERVICE ON CACHE BOOL "Load the application UI project from firmware" FORCE)
# Use the manifest's registry component names in both CMake expansion phases.
set(ENV{NA_CBOR_LVGL_COMPONENT} "lvgl__lvgl")
set(ENV{NA_CBOR_CBOR_COMPONENT} "espressif__cbor")
if("$ENV{IDF_COMPONENT_MANAGER}" STREQUAL "0")
    # Explicit offline builds reuse the already provisioned managed components.
    file(GLOB offline_component_files "${CMAKE_CURRENT_LIST_DIR}/managed_components/*/CMakeLists.txt")
    foreach(component_file IN LISTS offline_component_files)
        get_filename_component(component_dir "${component_file}" DIRECTORY)
        list(APPEND EXTRA_COMPONENT_DIRS "${component_dir}")
    endforeach()
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Auto_BLETPMS_ELM327)
if("$ENV{IDF_COMPONENT_MANAGER}" STREQUAL "0")
    # This dependency normally comes from the GT911 component manifest.
    idf_component_get_property(gt911_lib espressif__esp_lcd_touch_gt911 COMPONENT_LIB)
    idf_component_get_property(touch_lib espressif__esp_lcd_touch COMPONENT_LIB)
    target_link_libraries(${gt911_lib} PUBLIC ${touch_lib})
endif()
