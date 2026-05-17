idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "include"
                       REQUIRES sdio_slave wifi_manager ble_gatt protocol ota_manager
                       PRIV_REQUIRES freertos nvs_flash esp_timer
                       )