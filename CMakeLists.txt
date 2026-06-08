# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

# ESP_IDF_VERSION must be set before IDF include so that esp_wifi_remote Kconfig
# can resolve: orsource "./idf_v$ESP_IDF_VERSION/Kconfig.wifi.in"
# Without this the WiFi config block is silently skipped → sdkconfig loses
# CONFIG_ESP_WIFI_* symbols → transport falls back to SPI defaults instead of SDIO.
if(NOT DEFINED ENV{ESP_IDF_VERSION})
    set(ENV{ESP_IDF_VERSION} "5.4")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

add_compile_options(-Wno-format)
add_compile_options(-Wno-ignored-qualifiers)
add_compile_options(-Wno-attributes)
add_compile_options(-Wno-unused-function)
project(guition_supla_panel)
