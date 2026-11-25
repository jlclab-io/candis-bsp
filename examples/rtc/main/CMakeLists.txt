set(RTC_SLEEP_COMPONENT esp_hw_support)
if(EXISTS "$ENV{IDF_PATH}/components/esp_sleep/CMakeLists.txt")
    set(RTC_SLEEP_COMPONENT esp_sleep)
endif()

idf_component_register(SRCS "main.c" "wake.c"
                       PRIV_REQUIRES candis_example_usb esp_driver_gpio esp_rom ${RTC_SLEEP_COMPONENT})
