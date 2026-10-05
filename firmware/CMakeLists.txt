# WO_30109 新風控制器 V4 韌體（ESP32-C6，ESP-IDF v5.5.5 ＋ esp-matter release/v1.6，Matter over Thread）
# 需要環境變數 ESP_MATTER_PATH（. ~/esp/esp-matter/export.sh）。V3.x 的 Zigbee 版本在標籤 v3.4。
cmake_minimum_required(VERSION 3.16)
if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR "請先設定 ESP_MATTER_PATH（. ~/esp/esp-matter/export.sh）")
endif()
set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)
set(PROJECT_VER "4.0.0")
set(PROJECT_VER_NUMBER 40000)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include(${ESP_MATTER_PATH}/examples/common/cmake_common/components_include.cmake)
set(EXTRA_COMPONENT_DIRS
    components
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components"
    ${extra_components_dirs_append})
project(wo30109_fresh_air)
idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)
