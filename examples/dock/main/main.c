/* SPDX-License-Identifier: Apache-2.0 */
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <string.h>
#include "bsp/candis.h"
#include "bsp/candis_audio.h"
#include "bsp/candis_dock.h"
#include "bsp/candis_imu.h"
#include "bsp/candis_power.h"
#include "candis_example_usb.h"
#include "display.h"
#include "driver/gpio.h"
#include "eeprom.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tf_verify.h"
#include "tg28_esp.h"

static const char *TAG = "dock_demo";
enum { CELL_LIMIT_MA = 200, BOARD_LDO_MV = 3300 };

static candis_baseboard_info_t s_identity;
static bool s_dock;
static bool s_fixture;
static bool s_detect;
static bool s_mount;
static bool s_display;
static bool s_panel_only;
static bool s_audio;
#if BSP_CAPS_IMU
static bool s_imu_cleanup;
#endif
static int s_card_state = -1;
static bool s_card_sample;
static unsigned s_card_samples;
static i2c_master_bus_handle_t s_pmu_bus;
static tg28_esp_t s_pmu;

typedef struct {
    tg28_esp_status_t status;
    tg28_esp_charger_t charger;
    uint8_t rails;
    uint16_t aldo1_mv;
    uint16_t aldo2_mv;
    uint32_t vbat_uv;
    bool vbat_valid;
} power_sample_t;

static esp_err_t release_pmu(void)
{
    ESP_RETURN_ON_ERROR(tg28_esp_close(&s_pmu), TAG, "detach PMU");
    if (!s_pmu_bus) {
        return ESP_OK;
    }
    const esp_err_t err = candis_i2c_release(s_pmu_bus);
    if (err == ESP_OK) {
        s_pmu_bus = NULL;
    }
    return err;
}

static esp_err_t read_power(power_sample_t *sample, bool details)
{
    ESP_RETURN_ON_ERROR(tg28_esp_read_status(&s_pmu, &sample->status), TAG, "TG28 status");
    ESP_RETURN_ON_ERROR(tg28_esp_read_charger(&s_pmu, &sample->charger), TAG, "TG28 charger");
    ESP_RETURN_ON_ERROR(tg28_esp_read_outputs(&s_pmu, &sample->rails), TAG, "TG28 rails");
    ESP_RETURN_ON_ERROR(tg28_esp_read_rail_voltage(&s_pmu, TG28_ESP_RAIL_ALDO1, &sample->aldo1_mv),
                        TAG, "ALDO1 setpoint");
    ESP_RETURN_ON_ERROR(tg28_esp_read_rail_voltage(&s_pmu, TG28_ESP_RAIL_ALDO2, &sample->aldo2_mv),
                        TAG, "ALDO2 setpoint");
    if (details) {
        uint8_t channels;
        ESP_RETURN_ON_ERROR(tg28_esp_read_adc_mask(&s_pmu, &channels), TAG, "TG28 ADC configuration");
        if (channels & TG28_ESP_ADC_VBAT) {
            ESP_RETURN_ON_ERROR(tg28_esp_read_adc_uv(&s_pmu, TG28_ESP_ADC_VBAT, &sample->vbat_uv),
                                TAG, "battery voltage ADC");
            sample->vbat_valid = true;
        }
    }
    return ESP_OK;
}

static esp_err_t power_sample(power_sample_t *sample, bool details)
{
    *sample = (power_sample_t){0};
    ESP_RETURN_ON_ERROR(release_pmu(), TAG, "finish pending PMU release");
    ESP_RETURN_ON_ERROR(candis_i2c_acquire(&s_pmu_bus), TAG, "acquire Candis I2C");
    const tg28_esp_config_t config = TG28_ESP_CONFIG_DEFAULT();
    esp_err_t err = tg28_esp_open(&s_pmu, s_pmu_bus, &config);
    if (err == ESP_OK) {
        err = read_power(sample, details);
    }
    const esp_err_t cleanup = release_pmu();
    return err == ESP_OK ? cleanup : err;
}

static void log_power(const power_sample_t *power)
{
    ESP_LOGI(TAG, "POWER vbus=%d system=%d battery=%d direction=%d phase=%d "
             "chg_enabled=%d cc_setting_ma=%u precharge_setting_ma=%u cv_setting_mv=%u input_setting_ma=%u "
             "input_current_limit=%d input_voltage_limit=%d vbat_valid=%d vbat_uv=%" PRIu32,
             power->status.vbus_good, power->status.system_on, power->status.battery_present,
             power->status.direction, power->status.charge_state, power->charger.enabled,
             power->charger.current_ma, power->charger.precharge_ma, power->charger.voltage_mv,
             power->charger.input_limit_ma, power->status.input_current_limit,
             power->status.input_voltage_limit, power->vbat_valid, power->vbat_uv);
    ESP_LOGI(TAG, "RAILS raw=0x%02x dc1sw=%d aldo1=%d aldo2=%d aldo3=%d aldo1_mv=%u aldo2_mv=%u",
             power->rails, !!(power->rails & TG28_ESP_ENABLE_DC1SW), !!(power->rails & TG28_ESP_ENABLE_ALDO1),
             !!(power->rails & TG28_ESP_ENABLE_ALDO2), !!(power->rails & TG28_ESP_ENABLE_ALDO3),
             power->aldo1_mv, power->aldo2_mv);
}

static esp_err_t require_usb(power_sample_t *power)
{
    ESP_RETURN_ON_ERROR(power_sample(power, false), TAG, "check USB supply");
    const tg28_esp_status_t *status = &power->status;
    const tg28_esp_charger_t *charger = &power->charger;
    if (status->vbus_good && status->system_on &&
        (!status->battery_present ||
         (status->direction != TG28_ESP_BATTERY_DISCHARGING &&
          status->direction != TG28_ESP_BATTERY_DIRECTION_UNKNOWN &&
          (!charger->enabled || (charger->current_ma < CELL_LIMIT_MA && charger->precharge_ma < CELL_LIMIT_MA))))) {
        return ESP_OK;
    }
    log_power(power);
    ESP_LOGW(TAG, "A usable USB supply and charge setpoint margin are required for this test");
    return ESP_ERR_INVALID_STATE;
}

static esp_err_t identify(void)
{
    candis_baseboard_info_t identity;
    const esp_err_t err = candis_dock_probe(&identity);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "EEPROM verified name=%.32s hw=0x%04x format=%u",
                 identity.name, identity.hardware_revision, identity.format_revision);
    } else {
        ESP_LOGW(TAG, "EEPROM descriptor: %s", esp_err_to_name(err));
    }
    return err;
}

static esp_err_t close_expansion(void)
{
    ESP_RETURN_ON_ERROR(dock_eeprom_release(), TAG, "release U10 diagnostic transport");
    if (s_mount) {
        ESP_RETURN_ON_ERROR(candis_storage_unmount(), TAG, "unmount TF");
        s_mount = false;
    }
    if (s_detect) {
        ESP_RETURN_ON_ERROR(candis_storage_detect_stop(), TAG, "release TF card detect");
        s_detect = false;
    }
    s_card_state = -1;
    s_card_samples = 0;
    if (s_fixture) {
        ESP_RETURN_ON_ERROR(candis_expansion_power_release(), TAG, "release fixture DC1SW");
        s_fixture = false;
    }
    const esp_err_t err = candis_dock_close();
    if (err == ESP_OK) {
        s_dock = false;
    }
    return err;
}

static esp_err_t open_expansion(bool fixture)
{
    if (s_dock || s_fixture || s_detect) {
        return ESP_ERR_INVALID_STATE;
    }
    power_sample_t power;
    ESP_RETURN_ON_ERROR(require_usb(&power), TAG, "power expansion");
    esp_err_t err;
    if (fixture) {
        s_fixture = true;
        err = candis_expansion_power_acquire(true);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Validation fixture profile selected, TF CD on GPIO35");
        }
    } else {
        err = candis_dock_open(true, &s_identity);
        s_dock = err == ESP_OK;
        if (s_dock) {
            ESP_LOGI(TAG, "EEPROM verified name=%.32s hw=0x%04x format=%u",
                     s_identity.name, s_identity.hardware_revision, s_identity.format_revision);
        }
    }
    if (err == ESP_OK) {
        s_detect = true;
        err = fixture ? candis_storage_detect_start(CANDIS_STORAGE_FIXTURE)
                      : candis_dock_detect_start(&s_identity);
    }
    if (err != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(close_expansion());
    }
    return err;
}

static esp_err_t probe_modules(void)
{
    if (!s_dock) {
        return ESP_ERR_INVALID_STATE;
    }
    candis_dock_module_scan_t scan;
    ESP_RETURN_ON_ERROR(candis_dock_probe_modules(&s_identity, &scan), TAG, "probe module addresses");
    ESP_LOGI(TAG, "MODULES 0x51=%s 0x52=%s", scan.address_51 ? "ACK" : "no ACK", scan.address_52 ? "ACK" : "no ACK");
    return ESP_OK;
}

static void poll_card(void)
{
    if (!s_detect) {
        return;
    }
    bool present;
    if (candis_storage_detect_read(&present) != ESP_OK) {
        return;
    }
    if (s_card_sample != present) {
        s_card_sample = present;
        s_card_samples = 0;
    }
    if (s_card_samples < 3 && ++s_card_samples == 3 && s_card_state != present) {
        s_card_state = present;
        ESP_LOGI(TAG, "TF CD %s", present ? "inserted" : "removed");
    }
}

static esp_err_t count_entries(void)
{
    DIR *directory = opendir(CANDIS_EXAMPLE_TF_PATH);
    ESP_RETURN_ON_FALSE(directory, ESP_FAIL, TAG, "open TF directory: errno=%d", errno);
    unsigned entries = 0;
    errno = 0;
    while (entries < 16 && readdir(directory)) {
        ++entries;
    }
    const int read_error = errno;
    const int closed = closedir(directory);
    ESP_LOGI(TAG, "TF directory entries=%u read_errno=%d close=%d", entries, read_error, closed);
    return read_error || closed ? ESP_FAIL : ESP_OK;
}

static esp_err_t test_card(bool write)
{
    if (!s_detect || s_mount) {
        return ESP_ERR_INVALID_STATE;
    }
    power_sample_t power;
    ESP_RETURN_ON_ERROR(require_usb(&power), TAG, "power TF test");
    bool present;
    ESP_RETURN_ON_ERROR(candis_storage_detect_read(&present), TAG, "read GPIO39");
    ESP_RETURN_ON_FALSE(present, ESP_ERR_NOT_FOUND, TAG, "TF slot empty");

    const candis_storage_mount_config_t config = {.read_only = !write};
    sdmmc_card_t *card;
    s_mount = true;
    const esp_err_t mount = s_dock
        ? candis_dock_mount_with_config(&s_identity, CANDIS_EXAMPLE_TF_PATH, &config, &card)
        : candis_storage_mount_with_config(CANDIS_EXAMPLE_TF_PATH, CANDIS_STORAGE_FIXTURE, &config, &card);
    esp_err_t io = mount;
    if (mount == ESP_OK) {
        const uint64_t mib = (uint64_t)card->csd.capacity * card->csd.sector_size / (1024 * 1024);
        ESP_LOGI(TAG, "TF card_mib=%" PRIu64 " width=%u clock_khz=%d", mib,
                 1U << card->log_bus_width, card->real_freq_khz);
        io = write ? candis_example_tf_verify() : count_entries();
    }
    const esp_err_t unmount = candis_storage_unmount();
    s_mount = unmount != ESP_OK;
    ESP_LOGI(TAG, "TF %s mount=%s io=%s unmount=%s", write ? "write" : "read",
             esp_err_to_name(mount), esp_err_to_name(io), esp_err_to_name(unmount));
    return io == ESP_OK ? unmount : io;
}

static esp_err_t audio_block(const int16_t *pcm)
{
    size_t sent;
    ESP_RETURN_ON_ERROR(candis_audio_write(pcm, CANDIS_AUDIO_PLAYBACK_BLOCK_FRAMES, &sent, 100),
                        TAG, "write I2S");
    return sent == CANDIS_AUDIO_PLAYBACK_BLOCK_FRAMES ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static esp_err_t silence(void)
{
    const int16_t pcm[CANDIS_AUDIO_PLAYBACK_BLOCK_FRAMES * CANDIS_AUDIO_CHANNELS] = {0};
    for (unsigned block = 0; block < 10; ++block) {
        ESP_RETURN_ON_ERROR(audio_block(pcm), TAG, "send silence");
    }
    return ESP_OK;
}

static esp_err_t play(unsigned channel)
{
    enum { BLOCK = CANDIS_AUDIO_PLAYBACK_BLOCK_FRAMES, FRAMES = CANDIS_AUDIO_RATE * 3,
           FADE_FRAMES = CANDIS_AUDIO_RATE / 40, TONE_HZ = 1000, PEAK_PCM = 12000 };
    int16_t pcm[BLOCK * CANDIS_AUDIO_CHANNELS] = {0};
    for (unsigned frame = 0; frame < FRAMES; frame += BLOCK) {
        if ((frame % (CANDIS_AUDIO_RATE / 5)) == 0) {
            power_sample_t power;
            ESP_RETURN_ON_ERROR(require_usb(&power), TAG, "check supply during tone");
        }
        for (unsigned i = 0; i < BLOCK; ++i) {
            unsigned sample = frame + i;
            unsigned remaining = FRAMES - sample - 1;
            unsigned edge = sample < remaining ? sample : remaining;
            const float fade = edge < FADE_FRAMES ? (float)edge / FADE_FRAMES : 1.0f;
            pcm[i * CANDIS_AUDIO_CHANNELS + channel] =
                (int16_t)(PEAK_PCM * fade * sinf(6.28318531f * TONE_HZ * sample / CANDIS_AUDIO_RATE));
        }
        ESP_RETURN_ON_ERROR(audio_block(pcm), TAG, "send tone");
    }
    return silence();
}

static esp_err_t tone(unsigned channel, unsigned volume)
{
    if (s_audio) {
        return ESP_ERR_INVALID_STATE;
    }
    power_sample_t power;
    ESP_RETURN_ON_ERROR(require_usb(&power), TAG, "power audio");
    ESP_RETURN_ON_FALSE(power.aldo2_mv == BOARD_LDO_MV, ESP_ERR_INVALID_STATE, TAG, "unexpected ALDO2 setpoint");
    s_audio = true;
    esp_err_t err = candis_audio_open(true);
    if (err == ESP_OK) {
        err = candis_audio_set_volume(volume);
    }
    if (err == ESP_OK) {
        err = silence();
    }
    if (err == ESP_OK) {
        err = candis_audio_set_output(true);
    }
    if (err == ESP_OK) {
        err = play(channel);
    }
    const esp_err_t close = candis_audio_close();
    s_audio = close != ESP_OK;
    ESP_LOGI(TAG, "AUDIO channel=%c volume=%u playback=%s close=%s", channel == 0 ? 'L' : 'R', volume,
             esp_err_to_name(err), esp_err_to_name(close));
    return err == ESP_OK ? close : err;
}

static esp_err_t stop_display(void)
{
    const esp_err_t err = dock_display_stop();
    s_display = err != ESP_OK;
    if (err == ESP_OK) {
        s_panel_only = false;
    }
    return err;
}

static esp_err_t start_display(bool touch)
{
    if (s_display) {
        return ESP_ERR_INVALID_STATE;
    }
    power_sample_t power;
    ESP_RETURN_ON_ERROR(require_usb(&power), TAG, "power display");
    ESP_RETURN_ON_FALSE(power.aldo1_mv == BOARD_LDO_MV, ESP_ERR_INVALID_STATE, TAG, "unexpected ALDO1 setpoint");
    s_display = true;
    const esp_err_t err = dock_display_start(touch);
    if (err != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(stop_display());
    } else {
        s_panel_only = !touch;
    }
    return err;
}

static esp_err_t probe_touch(void)
{
    if (!s_panel_only) {
        return ESP_ERR_INVALID_STATE;
    }
    /* CB U16.18 is CTP_RST, held low by R28 until driven. Match CST9220 reset timing. */
    const gpio_num_t reset = GPIO_NUM_3;
    const gpio_config_t gpio = {.pin_bit_mask = 1ULL << reset, .mode = GPIO_MODE_INPUT_OUTPUT};
    ESP_RETURN_ON_ERROR(release_pmu(), TAG, "finish pending I2C release");
    ESP_RETURN_ON_ERROR(candis_i2c_acquire(&s_pmu_bus), TAG, "acquire Candis I2C");
    esp_err_t err = gpio_set_level(reset, 0);
    if (err == ESP_OK) {
        err = gpio_config(&gpio);
    }
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(10));
        err = gpio_set_level(reset, 1);
        if (err == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(100));
            err = i2c_master_probe(s_pmu_bus, 0x5a, 30);
            ESP_LOGI(TAG, "TOUCH reset=%d probe 0x5a=%s", gpio_get_level(reset), esp_err_to_name(err));
        }
    }
    const esp_err_t pins = gpio_reset_pin(reset);
    const esp_err_t release = release_pmu();
    return err != ESP_OK ? err : pins != ESP_OK ? pins : release;
}

static esp_err_t release_all(void)
{
#if BSP_CAPS_IMU
    esp_err_t imu = ESP_OK;
    if (s_imu_cleanup) {
        imu = candis_imu_stop();
        s_imu_cleanup = imu != ESP_OK;
    }
#endif
    esp_err_t audio = ESP_OK;
    if (s_audio) {
        audio = candis_audio_close();
        s_audio = audio != ESP_OK;
    }
    const esp_err_t display = stop_display();
    const esp_err_t expansion = close_expansion();
    const esp_err_t pmu = release_pmu();
#if BSP_CAPS_IMU
    if (imu != ESP_OK) {
        return imu;
    }
#endif
    if (audio != ESP_OK) {
        return audio;
    }
    if (display != ESP_OK) {
        return display;
    }
    return expansion == ESP_OK ? pmu : expansion;
}

static esp_err_t show_power(void)
{
    power_sample_t power;
    ESP_RETURN_ON_ERROR(power_sample(&power, true), TAG, "read power");
    log_power(&power);
    return ESP_OK;
}

static esp_err_t tone_dock(void)
{
    if (!s_dock) {
        return ESP_ERR_INVALID_STATE;
    }
    candis_dock_speaker_channel_t channel;
    ESP_RETURN_ON_ERROR(candis_dock_get_speaker_channel(&s_identity, &channel), TAG, "resolve CN3");
    return tone(channel, 45);
}

#if BSP_CAPS_IMU
static esp_err_t motion(void)
{
    if (!s_dock || s_imu_cleanup) {
        return ESP_ERR_INVALID_STATE;
    }
    s_imu_cleanup = true;
    esp_err_t err = candis_imu_start(false);
    if (err == ESP_OK) {
        candis_imu_sample_t sample;
        vTaskDelay(pdMS_TO_TICKS(100) + 1);
        err = candis_imu_read(&sample);
        if (err == ESP_OK) {
            const float *a = sample.acceleration_g;
            const float *g = sample.angular_velocity_dps;
            ESP_LOGI(TAG, "BMI270 a=(%.3f,%.3f,%.3f) g w=(%.3f,%.3f,%.3f) dps ticks=%" PRIu32,
                     a[0], a[1], a[2], g[0], g[1], g[2], sample.sensor_ticks);
            if (!a[0] && !a[1] && !a[2]) {
                err = ESP_ERR_INVALID_RESPONSE;
            }
        }
        if (err == ESP_OK) {
            float celsius;
            const esp_err_t temp = candis_imu_read_temperature(&celsius);
            if (temp == ESP_OK) {
                ESP_LOGI(TAG, "BMI270 die temperature %.2f C", celsius);
            } else if (temp != ESP_ERR_NOT_FINISHED) {
                err = temp;
            }
        }
    }
    const esp_err_t stop = candis_imu_stop();
    s_imu_cleanup = stop != ESP_OK;
    return err == ESP_OK ? stop : err;
}
#endif

static esp_err_t selftest(void)
{
    ESP_LOGI(TAG, "SELFTEST begin");
    esp_err_t err = show_power();
    if (err == ESP_OK) {
        err = open_expansion(false);
    }
    if (err == ESP_OK) {
        err = identify();
    }
    if (err == ESP_OK) {
        err = dock_eeprom_inspect();
    }
    if (err == ESP_OK) {
        err = probe_modules();
    }
    if (err == ESP_OK) {
        err = test_card(true);
    }
    if (err == ESP_OK) {
        err = start_display(true);
    }
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        err = tone(CANDIS_DOCK_SPEAKER_LEFT, 25);
    }
    if (err == ESP_OK) {
        err = tone(CANDIS_DOCK_SPEAKER_RIGHT, 25);
    }
    const esp_err_t release = release_all();
    ESP_LOGI(TAG, "SELFTEST result=%s release=%s", esp_err_to_name(err), esp_err_to_name(release));
    return err == ESP_OK ? release : err;
}

static void show_help(void)
{
    ESP_LOGI(TAG, "Commands: power | dock | fixture | id | eeprom | modules | tf | tf write |"
             " screen on | screen panel | touch probe | screen off | screen full | screen partial |"
             " tone dock | tone l | tone r | tone l loud | tone r loud | tone l 100 | tone r 100 |"
             " selftest | echo TEXT | release");
#if BSP_CAPS_IMU
    ESP_LOGI(TAG, "imu: start, read and release BMI270 on the identified Dock");
#endif
}

static void run_command(const char *line)
{
    esp_err_t err;
    if (!strcmp(line, "power")) {
        err = show_power();
    } else if (!strcmp(line, "dock") || !strcmp(line, "fixture")) {
        err = open_expansion(!strcmp(line, "fixture"));
    } else if (!strcmp(line, "id")) {
        err = identify();
    } else if (!strcmp(line, "eeprom")) {
        err = s_dock ? dock_eeprom_inspect() : ESP_ERR_INVALID_STATE;
    } else if (!strcmp(line, "modules")) {
        err = probe_modules();
    } else if (!strcmp(line, "tf") || !strcmp(line, "tf write")) {
        err = test_card(!strcmp(line, "tf write"));
    } else if (!strcmp(line, "screen on") || !strcmp(line, "screen panel")) {
        err = start_display(!strcmp(line, "screen on"));
    } else if (!strcmp(line, "touch probe")) {
        err = probe_touch();
    } else if (!strcmp(line, "screen off")) {
        err = stop_display();
    } else if (!strcmp(line, "screen full") || !strcmp(line, "screen partial")) {
        err = dock_display_mode(!strcmp(line, "screen full"));
    } else if (!strcmp(line, "tone dock")) {
        err = tone_dock();
#if BSP_CAPS_IMU
    } else if (!strcmp(line, "imu")) {
        err = motion();
#endif
    } else if (!strcmp(line, "tone l") || !strcmp(line, "tone r") ||
               !strcmp(line, "tone l loud") || !strcmp(line, "tone r loud")) {
        err = tone(line[5] == 'r', strlen(line) == 6 ? 25 : 45);
    } else if (!strcmp(line, "tone l 100") || !strcmp(line, "tone r 100")) {
        err = tone(line[5] == 'r', 100);
    } else if (!strcmp(line, "selftest")) {
        err = selftest();
    } else if (!strncmp(line, "echo ", 5)) {
        ESP_LOGI(TAG, "ECHO %s", line + 5);
        return;
    } else if (!strcmp(line, "release")) {
        err = release_all();
    } else {
        show_help();
        return;
    }
    ESP_LOGI(TAG, "RESULT %s %s", line, esp_err_to_name(err));
}

static void read_commands(void)
{
    static char line[72];
    static size_t size;
    static bool overflow;
    char byte;
    /* Bound each poll so incoming CDC traffic cannot starve status checks. */
    for (unsigned i = 0; i < sizeof(line) && candis_example_usb_read(&byte); ++i) {
        if (byte == '\r' || byte == '\n') {
            if (size && !overflow) {
                line[size] = '\0';
                run_command(line);
            }
            size = 0;
            overflow = false;
            return;
        }
        if (byte < ' ' || byte > '~' || size == sizeof(line) - 1) {
            overflow = true;
        } else if (!overflow) {
            line[size++] = byte;
        }
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(candis_example_usb_start(CANDIS_EXAMPLE_USB_CONSOLE, true));
    ESP_ERROR_CHECK_WITHOUT_ABORT(show_power());
    show_help();
    bool held = false;
    int64_t next_card = 0;
    int64_t next_supply = 0;
    while (true) {
        if (!held) {
            held = candis_example_usb_begin_interruptible_operation();
        }
        if (held && candis_example_usb_download_pending()) {
            if (release_all() == ESP_OK) {
                candis_example_usb_end_operation();
                held = false;
            }
        } else if (held) {
            read_commands();
            const int64_t now = esp_timer_get_time();
            if (now >= next_supply && (s_display || s_dock || s_fixture || s_audio)) {
                next_supply = now + 500000;
                power_sample_t power;
                if (require_usb(&power) != ESP_OK) {
                    ESP_ERROR_CHECK_WITHOUT_ABORT(release_all());
                }
            }
            if (now >= next_card) {
                next_card = now + 40000;
                poll_card();
            }
            dock_display_report();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
