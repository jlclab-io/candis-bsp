set(candis_sources
    "src/candis.c"
    "src/candis_display.c"
    "src/candis_rtc.c"
    "src/candis_power.c"
    "src/candis_motor.c"
    "src/candis_usb.c"
    "src/candis_storage.c"
    "src/candis_baseboard.c"
    "src/candis_baseboard_bus.c")
set(candis_private_requires esp_driver_spi esp_driver_uart esp_timer)
if(CONFIG_CANDIS_AUDIO)
    list(APPEND candis_sources "src/candis_audio.c")
endif()
if(CONFIG_CANDIS_IMU)
    list(APPEND candis_sources "src/candis_imu.c")
endif()

idf_component_register(SRCS ${candis_sources}
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "priv_include"
                       REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_driver_ledc
                                esp_lcd sdmmc esp_driver_sdmmc esp_driver_sdspi fatfs
                       PRIV_REQUIRES ${candis_private_requires} tg28-esp)
