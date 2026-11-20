idf_component_register(SRCS "main.c" "display.c" "eeprom.c"
                       PRIV_REQUIRES candis_example_usb candis_example_tf esp_driver_gpio esp_timer)
