idf_component_register(SRCS "wifi_manager.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_wifi esp_netif nvs_flash
                       PRIV_REQUIRES freertos lwip esp_event
                       )