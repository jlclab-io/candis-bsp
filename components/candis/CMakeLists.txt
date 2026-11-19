set(candis_sources
    "candis.c"
    "candis_display.c"
    "candis_rtc.c"
    "candis_power.c"
    "candis_storage.c"
    "candis_baseboard.c"
    "candis_expansion.c")
set(candis_private_requires esp_driver_gpio esp_driver_spi esp_driver_i2s esp_lcd esp_timer fatfs esp_driver_sdmmc)
if(CONFIG_CANDIS_AUDIO)
    list(APPEND candis_sources "candis_audio.c")
endif()
if(CONFIG_CANDIS_IMU)
    list(APPEND candis_sources "candis_imu.c")
endif()

idf_component_register(SRCS ${candis_sources}
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2c sdmmc
                       PRIV_REQUIRES ${candis_private_requires})
