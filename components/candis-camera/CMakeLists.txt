idf_component_register(SRCS "src/candis_camera.c" "src/candis_camera_stream.c" "src/candis_ov5640.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "priv_include"
                       REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_ledc esp_video esp_cam_sensor)

if(NOT CONFIG_ESP_VIDEO_ENABLE_DVP_VIDEO_DEVICE)
    message(FATAL_ERROR "candis-camera requires CONFIG_ESP_VIDEO_ENABLE_DVP_VIDEO_DEVICE")
endif()
