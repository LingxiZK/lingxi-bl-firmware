idf_component_register(SRCS "ble_gatt.c"
                       INCLUDE_DIRS "include"
                       REQUIRES bt esp_gap_ble esp_gatt
                       PRIV_REQUIRES freertos
                       )