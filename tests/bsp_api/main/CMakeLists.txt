idf_component_register(SRCS "main.c" "api_test.c" "api_test_console.c" "api_symbols.c" "api_contract.c"
                            "api_public_surface.c"
                            "test_candis.c" "test_candis_bus.c" "test_candis_display.c"
                            "test_candis_audio.c" "test_candis_power_rtc.c"
                            "test_candis_peripherals.c" "test_camera.c" "test_dock.c"
                            "test_tg28.c"
                       INCLUDE_DIRS "."
                       REQUIRES candis candis-camera candis-dock tg28-esp candis_example_usb
                       PRIV_REQUIRES esp_driver_uart)
