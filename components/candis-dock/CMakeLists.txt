idf_component_register(SRCS "src/candis_dock.c" INCLUDE_DIRS "include" REQUIRES candis
                       PRIV_REQUIRES esp_driver_i2c esp_driver_uart)
