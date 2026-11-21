idf_component_register(SRCS "src/tg28_esp.c" "src/tg28_esp_charger.c" "src/tg28_esp_regulator.c"
                            "src/tg28_esp_power.c" "src/tg28_esp_thermal.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "priv_include"
                       REQUIRES esp_driver_i2c)
