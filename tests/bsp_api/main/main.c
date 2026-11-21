#include <assert.h>
#include "usb/usb_host.h"
#include "bsp/esp-bsp.h"

_Static_assert(BSP_CAPS_AUDIO && BSP_CAPS_IMU && BSP_CAPS_USB_DEVICE && BSP_CAPS_USB_HOST,
               "Build the BSP API test with all optional interfaces");
_Static_assert(!BSP_CAPS_SDCARD && !BSP_CAPS_MOTOR,
               "Optional baseboard capabilities must remain runtime capabilities");
_Static_assert(__builtin_types_compatible_p(__typeof__(&bsp_feature_enable),
                                            esp_err_t (*)(bsp_feature_t, bool)),
               "ESP-BSP feature enable signature");

void app_main(void)
{
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(bsp_i2c_init());
    bus = bsp_i2c_get_handle();
    if (bus) {
        ESP_ERROR_CHECK(bsp_i2c_deinit());
    }
    candis_power_state_t rails;
    candis_supply_state_t supply;
    uint32_t voltage = 0;
    (void)candis_power_read(&rails);
    (void)candis_power_read_supply(&supply);
    (void)candis_power_read_battery_uv(&voltage);
    candis_capability_state_t capability;
    candis_baseboard_info_t baseboard;
    (void)candis_baseboard_probe_state(CANDIS_BASEBOARD_BB, &capability, &baseboard);
    const bsp_display_cfg_t ui = {.lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG()};
    lv_display_t *display = bsp_display_start_with_config(&ui);
    if (display) {
        (void)bsp_display_get_input_dev();
        bsp_display_rotate(display, LV_DISPLAY_ROTATION_0);
        (void)bsp_display_brightness_set(40);
        bsp_display_stop(display);
    }
    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_io_handle_t io;
    if (bsp_display_new(NULL, &panel, &io) == ESP_OK) {
        const uint16_t pixels[4] = {0};
        (void)esp_lcd_panel_disp_on_off(panel, true);
        (void)esp_lcd_panel_draw_bitmap(panel, 0, 0, 2, 2, pixels);
        (void)esp_lcd_panel_del(panel);
    }
    bsp_lcd_handles_t handles;
    if (bsp_display_new_with_handles(NULL, &handles) == ESP_OK) {
        bsp_display_delete();
    }
    (void)bsp_display_enter_sleep();
    (void)bsp_display_exit_sleep();
    esp_lcd_touch_handle_t touch = NULL;
    if (bsp_touch_new(NULL, &touch) == ESP_OK) {
        if (esp_lcd_touch_del(touch) != ESP_OK) {
            (void)bsp_touch_del(NULL);
        }
    }
    (void)candis_touch_new_with_config(NULL, NULL);
    if (bsp_sdcard_mount() == ESP_OK && bsp_sdcard_get_handle()) {
        (void)candis_storage_check_card();
        (void)bsp_sdcard_unmount();
    }
    (void)bsp_feature_enable(BSP_FEATURE_SD, false);
    sdmmc_host_t sd_host;
    sdmmc_slot_config_t sd_slot;
    bsp_sdcard_get_sdmmc_host(SDMMC_HOST_SLOT_0, &sd_host);
    bsp_sdcard_sdmmc_get_slot(SDMMC_HOST_SLOT_0, &sd_slot);
    bsp_sdcard_get_sdspi_host(SPI2_HOST, &sd_host);
    sdspi_device_config_t sdspi_slot;
    bsp_sdcard_sdspi_get_slot(SPI2_HOST, &sdspi_slot);
    (void)bsp_sdcard_sdspi_mount(NULL);
#if BSP_CAPS_IMU
    if (candis_imu_start(false) == ESP_OK) {
        candis_imu_sample_t sample;
        (void)candis_imu_read(&sample);
        (void)candis_imu_get_state(&capability);
        if (candis_imu_suspend() == ESP_OK) {
            (void)candis_imu_resume();
        }
        (void)candis_imu_stop();
    }
#endif
#if BSP_CAPS_AUDIO
    if (bsp_audio_init(NULL) == ESP_OK) {
        (void)bsp_audio_get_codec_itf();
        esp_codec_dev_handle_t input = bsp_audio_codec_microphone_init();
        esp_codec_dev_handle_t output = bsp_audio_codec_speaker_init();
        assert(input && output == input);
        esp_codec_dev_sample_info_t format = {
            .sample_rate = CANDIS_AUDIO_RATE,
            .bits_per_sample = 16,
            .channel = CANDIS_AUDIO_CHANNELS,
            .channel_mask = 3,
            .mclk_multiple = 256,
        };
        if (esp_codec_dev_open(output, &format) == ESP_CODEC_DEV_OK) {
            (void)esp_codec_dev_close(output);
        }
        ESP_ERROR_CHECK(bsp_audio_deinit());
    }
#endif
    /* Check optional API linkage without starting a motor or sourcing USB power. */
    if (candis_motor_pulse(0, 0) != ESP_OK) {
        candis_motor_state_t state;
        (void)candis_motor_get_state(&state);
        (void)candis_motor_stop();
    }
#if BSP_CAPS_USB_DEVICE
    if (candis_usb_device_start() == ESP_OK) {
        size_t length;
        char one = 0;
        (void)candis_usb_device_read(&one, 1, &length);
        (void)candis_usb_device_write(&one, 1, &length);
        (void)candis_usb_device_stop();
    }
#endif
#if BSP_CAPS_USB_HOST
    (void)bsp_usb_host_start(BSP_USB_HOST_POWER_MODE_AUTO, true);
    (void)bsp_usb_host_stop();
#endif
}
