cmake_minimum_required(VERSION 3.16)
set(APP_BOARD "lcd7" CACHE STRING "lcd7, lilygo or headless")
if(APP_BOARD STREQUAL "lcd7" OR APP_BOARD STREQUAL "lilygo")
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.s3.defaults")
endif()
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/src"
    "${CMAKE_CURRENT_LIST_DIR}/../NaBTools"
    "${CMAKE_CURRENT_LIST_DIR}/../NaCBORUIforLVGL"
    "${CMAKE_CURRENT_LIST_DIR}/../NaWServer")
set(LVGL_CBOR_PROJECT_SERVICE ON CACHE BOOL "Load the application UI project from firmware" FORCE)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Auto_BLETPMS_ELM327)
