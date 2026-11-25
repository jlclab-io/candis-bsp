idf_component_register(SRCS "main.c"
                       PRIV_REQUIRES candis_example_usb candis tg28-esp
                                     esp_driver_gpio esp_driver_i2c esp_sleep esp_timer)
