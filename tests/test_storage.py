# SPDX-License-Identifier: Apache-2.0
import os
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory
import unittest


STUB = r"""
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "bsp/candis_baseboard.h"
typedef int gpio_num_t;
enum { GPIO_MODE_DISABLE, GPIO_MODE_INPUT, GPIO_PULLUP_DISABLE = 0, GPIO_PULLUP_ENABLE = 1 };
typedef struct { uint64_t pin_bit_mask; int mode, pull_up_en; } gpio_config_t;
enum {
    CANDIS_RAIL_EXPANSION, CANDIS_SD_CLK = 24, CANDIS_SD_CMD = 25,
    CANDIS_SD_D0 = 20, CANDIS_SD_D1 = 21, CANDIS_SD_D2 = 22, CANDIS_SD_D3 = 23,
    CANDIS_FIXTURE_SD_CD = 35, CANDIS_EXP_SD_CD = 39,
    SDMMC_HOST_SLOT_0 = 0, SDMMC_FREQ_DEFAULT = 20000,
    SDMMC_SLOT_NO_WP = -1, SDMMC_SLOT_FLAG_INTERNAL_PULLUP = 1,
    SDSPI_SLOT_NO_CD = -1, SDSPI_SLOT_NO_WP = -1, GPIO_NUM_NC = -1,
};
typedef struct { int rail; void *bus; } candis_power_t;
typedef struct {
    int slot, max_freq_khz, flags, command_timeout_ms;
    float io_voltage;
    void *pwr_ctrl_handle;
    esp_err_t (*init)(void);
    esp_err_t (*deinit_p)(int slot);
} sdmmc_host_t;
typedef struct { bool host_initialized; } sdmmc_host_state_t;
typedef struct { int clk, cmd, d0, d1, d2, d3, cd, wp, width, flags; } sdmmc_slot_config_t;
typedef int spi_host_device_t;
typedef struct { int host_id, gpio_cs, gpio_cd, gpio_wp, gpio_int; } sdspi_device_config_t;
typedef struct { int max_files; bool read_only, format_if_mount_failed; } esp_vfs_fat_mount_config_t;
typedef esp_vfs_fat_mount_config_t esp_vfs_fat_sdmmc_mount_config_t;
typedef struct { int marker; } sdmmc_card_t;
#define SDMMC_HOST_DEFAULT() {0}
#define SDMMC_SLOT_CONFIG_DEFAULT() {0}
#define SDSPI_HOST_DEFAULT() {0}
#define SDSPI_SLOT_NO_CD (-1)
#define SDSPI_SLOT_NO_WP (-1)
#define ESP_RETURN_ON_ERROR(call, tag, ...) do { (void)(tag); \
    esp_err_t error = (call); if (error != ESP_OK) return error; } while (0)
#undef ESP_ERROR_CHECK_WITHOUT_ABORT
#define ESP_ERROR_CHECK_WITHOUT_ABORT(call) ((void)(call))
#define ESP_LOGE(tag, ...) ((void)(tag))
esp_err_t gpio_config(const gpio_config_t *cfg);
int gpio_get_level(gpio_num_t pin);
esp_err_t gpio_reset_pin(gpio_num_t pin);
esp_err_t gpio_pullup_dis(gpio_num_t pin);
bool candis_control_enter(void);
void candis_control_leave(void);
esp_err_t candis_power_acquire(candis_power_t *lease, bool manage, unsigned settle_ms);
esp_err_t candis_power_release(candis_power_t *lease);
esp_err_t candis_baseboard_probe_locked(candis_baseboard_info_t *info);
esp_err_t sdmmc_host_get_state(sdmmc_host_state_t *state);
esp_err_t sdmmc_host_init(void);
esp_err_t sdmmc_host_deinit(void);
esp_err_t sdmmc_host_deinit_slot(int slot);
esp_err_t esp_vfs_fat_sdmmc_mount(const char *path, const sdmmc_host_t *host,
                                 const void *slot, const esp_vfs_fat_mount_config_t *cfg,
                                 sdmmc_card_t **card);
esp_err_t esp_vfs_fat_sdcard_unmount(const char *path, sdmmc_card_t *card);
"""

SOURCE = r"""
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include "bsp/candis_storage.h"
#include "esp_idf_version.h"

static bool locked, powered, inserted, detect_enabled, readonly, manage;
static bool host_started, slot_started, fail_slot;
static unsigned references, mounts, unmounts, reset_pins;
static int expected_pin = CANDIS_EXP_SD_CD;
static esp_err_t mount_error, input_error, reset_error, power_release_error;
static esp_err_t dock_result;
static esp_err_t console_result;
static sdmmc_card_t test_card;
static sdmmc_host_t mounted_host;

bool candis_control_enter(void)
{
    assert(!locked);
    locked = true;
    return true;
}

void candis_control_leave(void)
{
    assert(locked);
    locked = false;
}

esp_err_t candis_power_acquire(candis_power_t *lease, bool manage_power, unsigned settle_ms)
{
    assert(locked && !lease->bus && lease->rail == CANDIS_RAIL_EXPANSION);
    assert(settle_ms == 0 || settle_ms == 20);
    manage = manage_power;
    if (!powered && !manage_power) {
        return ESP_ERR_INVALID_STATE;
    }
    ++references;
    lease->bus = lease;
    return ESP_OK;
}

esp_err_t candis_power_release(candis_power_t *lease)
{
    assert(locked);
    if (!lease->bus) {
        return ESP_OK;
    }
    if (power_release_error != ESP_OK) {
        return power_release_error;
    }
    assert(references > 0);
    --references;
    lease->bus = NULL;
    return ESP_OK;
}

esp_err_t gpio_config(const gpio_config_t *cfg)
{
    assert(locked && references > 0 && cfg->pin_bit_mask == (1ULL << expected_pin));
    if (cfg->mode == GPIO_MODE_INPUT) {
        assert(cfg->pull_up_en == GPIO_PULLUP_ENABLE);
        if (input_error != ESP_OK) {
            return input_error;
        }
        detect_enabled = true;
    } else {
        assert(cfg->mode == GPIO_MODE_DISABLE && cfg->pull_up_en == 0);
        detect_enabled = false;
    }
    return ESP_OK;
}

esp_err_t gpio_reset_pin(gpio_num_t pin)
{
    assert(locked && references > 0 && pin >= CANDIS_SD_D0 && pin <= CANDIS_SD_CMD);
    if (reset_error != ESP_OK) {
        return reset_error;
    }
    ++reset_pins;
    return ESP_OK;
}

esp_err_t gpio_pullup_dis(gpio_num_t pin)
{
    assert(locked && references > 0 && pin >= CANDIS_SD_D0 && pin <= CANDIS_SD_CMD);
    return ESP_OK;
}

int gpio_get_level(gpio_num_t pin)
{
    assert(locked && detect_enabled && pin == expected_pin);
    return !inserted;
}

esp_err_t candis_baseboard_probe_locked(candis_baseboard_info_t *info)
{
    assert(locked && references);
    if (dock_result == ESP_OK) {
        *info = (candis_baseboard_info_t){.kind = CANDIS_BASEBOARD_BB, .hardware_revision = 0x0102};
    }
    return dock_result;
}

esp_err_t candis_expansion_i2c_check(void)
{
    assert(locked);
    return console_result;
}

esp_err_t sdmmc_host_get_state(sdmmc_host_state_t *state)
{
    assert(locked);
    state->host_initialized = host_started;
    return ESP_OK;
}

esp_err_t sdmmc_host_init(void)
{
    assert(locked && !host_started);
    host_started = true;
    return ESP_OK;
}

esp_err_t sdmmc_host_deinit(void)
{
    assert(locked && host_started && !slot_started);
    host_started = false;
    return ESP_OK;
}

esp_err_t sdmmc_host_deinit_slot(int slot)
{
    assert(locked && host_started && slot == SDMMC_HOST_SLOT_0);
    if (!slot_started) {
        return ESP_ERR_INVALID_ARG;
    }
    slot_started = false;
    host_started = false;
    detect_enabled = false;
    return ESP_OK;
}

esp_err_t esp_vfs_fat_sdmmc_mount(const char *path, const sdmmc_host_t *host,
                                 const void *slot_cfg, const esp_vfs_fat_mount_config_t *cfg,
                                 sdmmc_card_t **card)
{
    const sdmmc_slot_config_t *slot = slot_cfg;
    assert(locked && references > 0 && (!strcmp(path, "/card") || !strcmp(path, BSP_SD_MOUNT_POINT)));
    assert(host->max_freq_khz == SDMMC_FREQ_DEFAULT && slot->width == 4);
    assert(slot->cd == expected_pin && !cfg->format_if_mount_failed);
    assert(host->init() == ESP_OK);
    ++mounts;
    readonly = cfg->read_only;
    if (fail_slot) {
        assert(host->deinit_p(host->slot) == ESP_ERR_INVALID_ARG);
        return ESP_FAIL;
    }
    slot_started = true;
    if (mount_error != ESP_OK) {
        assert(host->deinit_p(host->slot) == ESP_OK);
        return mount_error;
    }
    mounted_host = *host;
    *card = &test_card;
    return ESP_OK;
}

esp_err_t esp_vfs_fat_sdcard_unmount(const char *path, sdmmc_card_t *card)
{
    assert(locked && references > 0 && (!strcmp(path, "/card") || !strcmp(path, BSP_SD_MOUNT_POINT)) &&
           card == &test_card);
    assert(mounted_host.deinit_p(mounted_host.slot) == ESP_OK);
    ++unmounts;
    return ESP_OK;
}

int main(void)
{
    sdmmc_host_t host = {0};
    sdmmc_slot_config_t slot = {0};
    sdspi_device_config_t spi_slot = {0};
    bsp_sdcard_get_sdmmc_host(SDMMC_HOST_SLOT_0, &host);
    bsp_sdcard_sdmmc_get_slot(SDMMC_HOST_SLOT_0, &slot);
    assert(host.slot == SDMMC_HOST_SLOT_0 && host.max_freq_khz == SDMMC_FREQ_DEFAULT);
    assert(slot.width == 4 && slot.clk == CANDIS_SD_CLK && slot.cd == CANDIS_EXP_SD_CD);
    bsp_sdcard_sdspi_get_slot(1, &spi_slot);
    assert(spi_slot.host_id == 1 && spi_slot.gpio_cs == GPIO_NUM_NC);
    assert(bsp_sdcard_sdspi_mount(NULL) == ESP_ERR_INVALID_ARG);

    const candis_storage_mount_config_t read_only = {.read_only = true};
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 2, 0)
    const candis_storage_mount_config_t *mount_config = &read_only;
    const bool expected_readonly = true;
#else
    const candis_storage_mount_config_t read_write = {0};
    const candis_storage_mount_config_t *mount_config = &read_write;
    const bool expected_readonly = false;
#endif
    sdmmc_card_t *card;
    bool present = true;
    assert(candis_storage_detect_start(CANDIS_STORAGE_EXPANSION) == ESP_ERR_INVALID_STATE);
    assert(references == 0 && !detect_enabled);

    powered = true;
    assert(candis_storage_detect_start(CANDIS_STORAGE_EXPANSION) == ESP_OK);
    assert(references == 1 && !manage && detect_enabled);
    assert(candis_storage_detect_read(&present) == ESP_OK && !present);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 2, 0)
    assert(candis_storage_mount_with_config("/card", CANDIS_STORAGE_EXPANSION, &read_only, &card) == ESP_ERR_NOT_FOUND);
#else
    assert(candis_storage_mount_with_config("/card", CANDIS_STORAGE_EXPANSION, &read_only, &card) == ESP_ERR_NOT_SUPPORTED);
#endif
    assert(mounts == 0 && card == NULL);
    assert(candis_storage_mount("/card", CANDIS_STORAGE_FIXTURE, false, &card) == ESP_ERR_INVALID_STATE);

    inserted = true;
    assert(candis_storage_detect_read(&present) == ESP_OK && present);
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 2, 0)
    card = &test_card;
    assert(candis_storage_mount_with_config("/card", CANDIS_STORAGE_EXPANSION, &read_only, &card) ==
           ESP_ERR_NOT_SUPPORTED);
    assert(card == NULL && mounts == 0 && references == 1 && detect_enabled);
#endif
    mount_error = ESP_FAIL;
    assert(candis_storage_mount_with_config("/card", CANDIS_STORAGE_EXPANSION, mount_config, &card) == ESP_FAIL);
    assert(readonly == expected_readonly && detect_enabled && references == 1 && card == NULL);
    mount_error = ESP_OK;
    assert(candis_storage_mount_with_config("/card", CANDIS_STORAGE_EXPANSION, mount_config, &card) == ESP_OK);
    assert(card == &test_card && readonly == expected_readonly && references == 2);
    assert(candis_storage_detect_stop() == ESP_ERR_INVALID_STATE);
    inserted = false;
    assert(candis_storage_detect_read(&present) == ESP_OK && !present);
    assert(candis_storage_check_card() == ESP_ERR_NOT_FOUND);
    inserted = true;
    assert(candis_storage_check_card() == ESP_ERR_NOT_FOUND); /* Requires unmount. */
    assert(candis_storage_unmount() == ESP_OK && unmounts == 1 && detect_enabled && references == 1);

    inserted = true;
    assert(candis_storage_mount_with_config("/card", CANDIS_STORAGE_EXPANSION, mount_config, &card) == ESP_OK);
    input_error = ESP_FAIL;
    assert(candis_storage_unmount() == ESP_FAIL && unmounts == 2 && references == 1);
    assert(candis_storage_detect_read(&present) == ESP_ERR_INVALID_STATE);
    input_error = ESP_OK;
    assert(candis_storage_unmount() == ESP_OK && unmounts == 2 && references == 1 && detect_enabled);
    assert(candis_storage_detect_read(&present) == ESP_OK && present);
    power_release_error = ESP_FAIL;
    assert(candis_storage_detect_stop() == ESP_FAIL && references == 1 && !detect_enabled);
    power_release_error = ESP_OK;
    assert(candis_storage_detect_stop() == ESP_OK && references == 0);

    inserted = true;
    assert(candis_storage_detect_start(CANDIS_STORAGE_EXPANSION) == ESP_OK && references == 1);
    assert(bsp_sdcard_mount() == ESP_OK && references == 2);
    input_error = ESP_FAIL;
    assert(bsp_sdcard_unmount() == ESP_FAIL && unmounts == 3 && references == 1);
    input_error = ESP_OK;
    assert(bsp_sdcard_unmount() == ESP_OK && unmounts == 3 && references == 1 && detect_enabled);
    assert(candis_storage_detect_read(&present) == ESP_OK && present);
    assert(candis_storage_detect_stop() == ESP_OK && references == 0);

    input_error = ESP_OK;
    expected_pin = CANDIS_FIXTURE_SD_CD;
    assert(candis_storage_mount("/card", CANDIS_STORAGE_FIXTURE, true, &card) == ESP_OK);
    assert(card == &test_card && !readonly && manage && references == 1);
    assert(candis_storage_unmount() == ESP_OK && references == 0 && reset_pins == 30);

    expected_pin = CANDIS_EXP_SD_CD;
    assert(bsp_sdcard_mount() == ESP_OK && bsp_sdcard_get_handle() == &test_card);
    assert(candis_storage_unmount() == ESP_ERR_INVALID_STATE && references == 1);
    reset_error = ESP_FAIL;
    assert(bsp_sdcard_unmount() == ESP_FAIL && references == 1 && unmounts == 5);
    assert(bsp_sdcard_get_handle() == NULL);
    assert(bsp_sdcard_mount() == ESP_ERR_INVALID_STATE);
    reset_error = ESP_OK;
    assert(bsp_sdcard_unmount() == ESP_OK && references == 0 && unmounts == 5);
    assert(bsp_sdcard_unmount() == ESP_OK);

    const esp_vfs_fat_sdmmc_mount_config_t read_only_mount = {
        .max_files = 9, .read_only = true, .format_if_mount_failed = false,
    };
    bsp_sdcard_cfg_t bsp_config = {.mount = &read_only_mount};
    assert(bsp_sdcard_sdmmc_mount(&bsp_config) == ESP_OK && readonly && mounts == 7);
    assert(bsp_sdcard_unmount() == ESP_OK && unmounts == 6);
    const esp_vfs_fat_sdmmc_mount_config_t format_mount = {
        .max_files = 1, .format_if_mount_failed = true,
    };
    bsp_config.mount = &format_mount;
    assert(bsp_sdcard_sdmmc_mount(&bsp_config) == ESP_ERR_NOT_SUPPORTED && mounts == 7);

    dock_result = ESP_ERR_NOT_FOUND;
    assert(bsp_feature_enable(BSP_FEATURE_SD, true) == ESP_ERR_NOT_FOUND && references == 0);
    assert(bsp_sdcard_mount() == ESP_ERR_NOT_FOUND && !detect_enabled && references == 0 && mounts == 7);
    dock_result = ESP_OK;
    console_result = ESP_ERR_INVALID_STATE;
    assert(bsp_feature_enable(BSP_FEATURE_SD, true) == ESP_ERR_INVALID_STATE && references == 0);
    console_result = ESP_OK;
    assert(bsp_feature_enable(BSP_FEATURE_SD, true) == ESP_OK && references == 1);
    assert(bsp_feature_enable(BSP_FEATURE_SD, true) == ESP_OK && references == 1);
    fail_slot = true;
    assert(bsp_sdcard_mount() == ESP_FAIL && !host_started && references == 1 && !detect_enabled);
    fail_slot = false;
    assert(bsp_sdcard_mount() == ESP_OK && references == 2 && host_started);
    assert(bsp_feature_enable(BSP_FEATURE_SD, false) == ESP_OK && references == 1 && host_started);
    assert(bsp_sdcard_unmount() == ESP_OK && !host_started && references == 0);
    host_started = true; /* The app owns the host, so the BSP leaves it alone. */
    assert(bsp_sdcard_mount() == ESP_ERR_INVALID_STATE && host_started && references == 0);
}
"""

FILE_TEST = r"""
#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include "tf_verify.h"

static bool present = true, remove_during_write, corrupt_read, fail_sync;
static unsigned syncs;

esp_err_t candis_storage_detect_read(bool *inserted)
{
    *inserted = present;
    return ESP_OK;
}

ssize_t __real_write(int fd, const void *data, size_t size);
ssize_t __wrap_write(int fd, const void *data, size_t size)
{
    ssize_t result = __real_write(fd, data, size);
    if (remove_during_write) {
        present = false;
    }
    return result;
}

ssize_t __real_read(int fd, void *data, size_t size);
ssize_t __wrap_read(int fd, void *data, size_t size)
{
    ssize_t result = __real_read(fd, data, size);
    if (corrupt_read && result > 0) {
        ((unsigned char *)data)[0] ^= 1;
    }
    return result;
}

int __real_fsync(int fd);
int __wrap_fsync(int fd)
{
    ++syncs;
    return fail_sync ? -1 : __real_fsync(fd);
}

int main(void)
{
    const char previous[] = CANDIS_EXAMPLE_TF_PATH "/CDS0000.TST";
    const char new_file[] = CANDIS_EXAMPLE_TF_PATH "/CDS0001.TST";
    int fd = open(previous, O_CREAT | O_WRONLY | O_EXCL, 0600);
    assert(fd >= 0 && write(fd, "keep", 4) == 4 && close(fd) == 0);
    assert(candis_example_tf_verify() == ESP_OK && syncs == 1);
    assert(access(new_file, F_OK) != 0);
    fd = open(previous, O_RDONLY);
    char contents[4];
    assert(fd >= 0 && read(fd, contents, 4) == 4 && !memcmp(contents, "keep", 4));
    assert(close(fd) == 0);

    corrupt_read = true;
    assert(candis_example_tf_verify() == ESP_ERR_INVALID_CRC && syncs == 2);
    assert(access(new_file, F_OK) != 0);
    corrupt_read = false;

    fail_sync = true;
    assert(candis_example_tf_verify() == ESP_FAIL && syncs == 3);
    assert(access(new_file, F_OK) != 0);
    fail_sync = false;

    remove_during_write = true;
    assert(candis_example_tf_verify() == ESP_ERR_NOT_FOUND && syncs == 3);
    assert(access(new_file, F_OK) == 0);
    assert(unlink(new_file) == 0 && unlink(previous) == 0);
}
"""


class StorageTest(unittest.TestCase):
    def test_card_detect_leases_and_read_only_mount(self):
        component = Path(__file__).resolve().parents[1] / "components/candis"
        idf_root = Path(os.environ["IDF_PATH"])
        idf = idf_root / "components/esp_common/include"
        with TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "sdkconfig.h").touch()
            (root / "stub.h").write_text(STUB)
            for name in ("candis_internal.h", "sdmmc_cmd.h", "driver/gpio.h",
                         "driver/sdmmc_host.h", "driver/sdspi_host.h", "esp_check.h", "esp_log.h",
                         "esp_vfs_fat.h"):
                header = root / name
                header.parent.mkdir(parents=True, exist_ok=True)
                header.write_text('#include "stub.h"\n')
            driver = root / "candis_storage.c"
            shutil.copyfile(component / "src" / driver.name, driver)
            test = root / "test.c"
            test.write_text(SOURCE)
            binary = root / "test"
            subprocess.run(
                ["cc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=undefined", "-fno-sanitize-recover=undefined", f"-I{root}",
                 f"-I{idf}", f"-I{component / 'include'}",
                 str(driver), str(test), "-o", str(binary)],
                check=True,
            )
            subprocess.run([binary], check=True)

    def test_tf_file_exclusive_creation_readback_and_removal(self):
        example = Path(__file__).resolve().parents[1] / "examples/common/candis_example_tf"
        idf = Path(os.environ["IDF_PATH"]) / "components/esp_common/include"
        with TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "sdkconfig.h").touch()
            (root / "bsp").mkdir()
            (root / "bsp/candis_storage.h").write_text(
                '#include <stdbool.h>\n#include "esp_err.h"\n'
                'esp_err_t candis_storage_detect_read(bool *present);\n'
            )
            (root / "esp_check.h").write_text(
                '#define ESP_RETURN_ON_ERROR(expr, tag, ...) do { (void)(tag); '
                'esp_err_t code = (expr); if (code != ESP_OK) return code; } while (0)\n'
                '#define ESP_RETURN_ON_FALSE(cond, err, tag, ...) do { (void)(tag); '
                'if (!(cond)) return err; } while (0)\n'
                '#define ESP_LOGI(tag, ...) ((void)(tag))\n'
            )
            medium = root / "card"
            medium.mkdir()
            header = (example / "include/tf_verify.h").read_text()
            (root / "tf_verify.h").write_text(header.replace('"/sdcard"', f'"{medium}"'))
            driver = root / "tf_verify.c"
            shutil.copyfile(example / driver.name, driver)
            test = root / "test.c"
            test.write_text(FILE_TEST)
            binary = root / "test"
            subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", f"-I{root}", f"-I{idf}",
                 "-Wl,--wrap=read,--wrap=write,--wrap=fsync", str(driver), str(test), "-o", str(binary)],
                check=True,
            )
            subprocess.run([binary], check=True)
