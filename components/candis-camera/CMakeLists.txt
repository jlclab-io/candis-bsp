idf_component_register(SRCS "candis_camera.c" "candis_camera_stream.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_ledc)

if(NOT CONFIG_ESP_VIDEO_ENABLE_DVP_VIDEO_DEVICE)
    message(FATAL_ERROR "candis-camera requires CONFIG_ESP_VIDEO_ENABLE_DVP_VIDEO_DEVICE")
endif()
