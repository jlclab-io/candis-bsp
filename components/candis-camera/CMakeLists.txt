idf_component_register(SRCS "src/candis_camera.c" "src/candis_camera_stream.c" "src/candis_camera_profile.c"
                              "src/profiles/candis_camera_profile_bf20a6.c"
                              "src/profiles/candis_camera_profile_bf3031.c"
                              "src/profiles/candis_camera_profile_bf3045.c"
                              "src/profiles/candis_camera_profile_bf3925.c"
                              "src/profiles/candis_camera_profile_bf3a03.c"
                              "src/profiles/candis_camera_profile_gc0308.c"
                              "src/profiles/candis_camera_profile_gc032a.c"
                              "src/profiles/candis_camera_profile_gc2145.c"
                              "src/profiles/candis_camera_profile_mt9d111.c"
                              "src/profiles/candis_camera_profile_ov2640.c"
                              "src/profiles/candis_camera_profile_ov3640.c"
                              "src/profiles/candis_camera_profile_ov3660.c"
                              "src/profiles/candis_camera_profile_ov5640.c"
                              "src/profiles/candis_camera_profile_sc030iot.c"
                              "src/profiles/candis_camera_profile_sc101iot.c"
                              "src/profiles/candis_camera_profile_sc2336.c"
                              "src/profiles/candis_camera_profile_sp0a39.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "priv_include"
                       REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_ledc esp_video esp_cam_sensor)

if(NOT CONFIG_ESP_VIDEO_ENABLE_DVP_VIDEO_DEVICE)
    message(FATAL_ERROR "candis-camera requires CONFIG_ESP_VIDEO_ENABLE_DVP_VIDEO_DEVICE")
endif()
