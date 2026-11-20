idf_component_register(SRCS "console.c" INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_stdio esp_ringbuf esp_timer esp_hal_usb soc)
