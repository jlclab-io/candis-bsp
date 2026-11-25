set(SLEEP_COMPONENT esp_hw_support)
if(EXISTS "$ENV{IDF_PATH}/components/esp_sleep/CMakeLists.txt")
    set(SLEEP_COMPONENT esp_sleep)
endif()

idf_component_register(SRCS "main.c"
                       PRIV_REQUIRES candis_example_usb candis tg28-esp
                                     esp_driver_gpio esp_driver_i2c ${SLEEP_COMPONENT} esp_timer)
