idf_component_register(SRCS "tg28_esp.c" "tg28_esp_charger.c" "tg28_esp_regulator.c"
                            "tg28_esp_power.c" "tg28_esp_thermal.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2c)
