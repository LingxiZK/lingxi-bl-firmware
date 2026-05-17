cmake_minimum_required(VERSION 3.16)

# ESP-IDF required
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS
    components/sdio_slave
    components/wifi_manager
    components/protocol
    components/ble_gatt
    components/ota_manager
    ../shared/protocol
    ../shared/utils
)

project(lingxi_bl_esp32)
