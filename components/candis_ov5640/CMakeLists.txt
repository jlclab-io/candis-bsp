set(sources "candis_ov5640.c")
if(CONFIG_CANDIS_OV5640_FIXTURE)
    list(APPEND sources "candis_ov5640_fixture.c")
endif()
idf_component_register(SRCS ${sources} INCLUDE_DIRS "include"
                       REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_ledc)
