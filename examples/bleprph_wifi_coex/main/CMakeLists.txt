idf_component_register(
    SRCS "main.c" "gatt_svr.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        bt
        candis_example_usb
        esp_http_client
        nvs_flash
)
