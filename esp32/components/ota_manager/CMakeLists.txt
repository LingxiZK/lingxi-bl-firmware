idf_component_register(SRCS "ota_manager.c"
                       INCLUDE_DIRS "include"
                       REQUIRES app_update esp_partition mbedtls
                       PRIV_REQUIRES freertos protocol
                       )