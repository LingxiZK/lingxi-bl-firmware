idf_component_register(SRCS "sdio_slave.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver esp_driver_sdio
                       PRIV_REQUIRES freertos esp_timer
                       )