idf_component_register(SRCS "protocol_handler.c"
                       INCLUDE_DIRS "include"
                       REQUIRES sdio_slave
                       PRIV_REQUIRES freertos
                       )