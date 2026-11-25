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
typedef int gpio_num_t;
enum {
    GPIO_MODE_DISABLE, GPIO_MODE_INPUT, GPIO_PULLUP_DISABLE = 0, GPIO_PULLUP_ENABLE = 1,
    GPIO_NUM_NC = -1, SDMMC_HOST_SLOT_0 = 0, SDMMC_FREQ_DEFAULT = 20000,
    CANDIS_RAIL_EXPANSION = 0,
    SDMMC_SLOT_NO_WP = -1, SDMMC_SLOT_FLAG_INTERNAL_PULLUP = 1,
    SDSPI_SLOT_NO_CD = -1, SDSPI_SLOT_NO_WP = -1,
};
typedef struct { uint64_t pin_bit_mask; int mode, pull_up_en; } gpio_config_t;
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
typedef struct { int rail; void *bus; } candis_power_t;
esp_err_t gpio_config(const gpio_config_t *cfg);
int gpio_get_level(gpio_num_t pin);
esp_err_t gpio_reset_pin(gpio_num_t pin);
esp_err_t gpio_pullup_dis(gpio_num_t pin);
bool candis_control_enter(void);
void candis_control_leave(void);
esp_err_t candis_power_acquire(candis_power_t *lease, bool manage, unsigned settle_ms);
esp_err_t candis_power_release(candis_power_t *lease);
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
#include <string.h>
#include <stdio.h>
#include "bsp/candis_storage.h"
#include "esp_idf_version.h"

enum { TEST_CD = 35, OTHER_CD = 17 };
static bool locked, powered, inserted = true, detect_enabled, readonly, manage;
static bool host_started, fail_mount;
static unsigned references, mounts, unmounts, resets;
static sdmmc_card_t test_card;
static int expected_cd = TEST_CD;

bool candis_control_enter(void) { assert(!locked); locked = true; return true; }
void candis_control_leave(void) { assert(locked); locked = false; }

esp_err_t candis_power_acquire(candis_power_t *lease, bool manage_power, unsigned settle_ms)
{
    assert(locked && !lease->bus && (settle_ms == 0 || settle_ms == 20));
    if (!powered && !manage_power) return ESP_ERR_INVALID_STATE;
    manage = manage_power;
    if (manage_power) powered = true;
    ++references;
    lease->bus = lease;
    return ESP_OK;
}

esp_err_t candis_power_release(candis_power_t *lease)
{
    assert(locked);
    if (!lease->bus) return ESP_OK;
    lease->bus = NULL;
    assert(references);
    --references;
    return ESP_OK;
}

esp_err_t gpio_config(const gpio_config_t *cfg)
{
    assert(locked && cfg->pin_bit_mask == (1ULL << expected_cd));
    detect_enabled = cfg->mode == GPIO_MODE_INPUT;
    assert(cfg->pull_up_en == (detect_enabled ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE));
    return ESP_OK;
}
int gpio_get_level(gpio_num_t pin) { assert(pin == expected_cd); return inserted ? 0 : 1; }
esp_err_t gpio_reset_pin(gpio_num_t pin) { assert(pin > GPIO_NUM_NC); ++resets; return ESP_OK; }
esp_err_t gpio_pullup_dis(gpio_num_t pin) { assert(pin > GPIO_NUM_NC); return ESP_OK; }
esp_err_t sdmmc_host_get_state(sdmmc_host_state_t *state) { state->host_initialized = host_started; return ESP_OK; }
esp_err_t sdmmc_host_init(void) { assert(locked && !host_started); host_started = true; return ESP_OK; }
esp_err_t sdmmc_host_deinit(void) { assert(locked && host_started); host_started = false; return ESP_OK; }
esp_err_t sdmmc_host_deinit_slot(int slot) { (void)slot; host_started = false; return ESP_OK; }

esp_err_t esp_vfs_fat_sdmmc_mount(const char *path, const sdmmc_host_t *host,
                                 const void *slot_ptr, const esp_vfs_fat_mount_config_t *cfg,
                                 sdmmc_card_t **card)
{
    assert(path && host && slot_ptr && cfg && card);
    const sdmmc_slot_config_t *slot = slot_ptr;
    assert(slot->clk == 24 && slot->cmd == 25 && slot->d0 == 20 && slot->d1 == 21 &&
           slot->d2 == 22 && slot->d3 == 23 && slot->cd == expected_cd && slot->width == 4);
    if (fail_mount) return ESP_FAIL;
    assert(host->init && host->init() == ESP_OK);
    readonly = cfg->read_only;
    ++mounts;
    *card = &test_card;
    return ESP_OK;
}
esp_err_t esp_vfs_fat_sdcard_unmount(const char *path, sdmmc_card_t *card)
{
    assert(path && card == &test_card);
    ++unmounts;
    return ESP_OK;
}

static candis_storage_config_t config_for(int cd, bool dc1sw)
{
    candis_storage_config_t config = CANDIS_STORAGE_CONFIG_DEFAULT();
    config.slot.clk = 24; config.slot.cmd = 25;
    config.slot.d0 = 20; config.slot.d1 = 21; config.slot.d2 = 22; config.slot.d3 = 23;
    config.slot.cd = cd; config.slot.wp = SDMMC_SLOT_NO_WP; config.slot.width = 4;
    config.slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    config.card_detect_active_low = true;
    config.use_dc1sw = dc1sw;
    return config;
}

int main(void)
{
    candis_storage_config_t bad = config_for(TEST_CD, true);
    bad.slot.d3 = GPIO_NUM_NC;
    assert(candis_storage_configure(&bad) == ESP_ERR_INVALID_ARG);
    bool present = false;
    assert(candis_storage_detect_start(true) == ESP_ERR_INVALID_STATE);

    candis_storage_config_t config = config_for(TEST_CD, true);
    assert(candis_storage_configure(&config) == ESP_OK);
    assert(candis_storage_detect_start(true) == ESP_OK && references == 1 && detect_enabled);
    assert(candis_storage_detect_read(&present) == ESP_OK && present);
    sdmmc_card_t *card = NULL;
    const candis_storage_mount_config_t ro = {.read_only = true};
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 2, 0)
    assert(candis_storage_mount_with_config("/card", &ro, &card) == ESP_OK);
    assert(card == &test_card && readonly && references == 2 && mounts == 1);
    assert(candis_storage_check_card() == ESP_OK);
    assert(candis_storage_detect_stop() == ESP_ERR_INVALID_STATE);
    assert(candis_storage_unmount() == ESP_OK && references == 1 && unmounts == 1);
#else
    assert(candis_storage_mount_with_config("/card", &ro, &card) == ESP_ERR_NOT_SUPPORTED);
    assert(card == NULL && !readonly && references == 1 && mounts == 0);
    assert(candis_storage_detect_stop() == ESP_OK && references == 0 && !detect_enabled);
    config = config_for(TEST_CD, true);
    assert(candis_storage_configure(&config) == ESP_OK);
    assert(candis_storage_mount("/card", true, &card) == ESP_OK &&
           card == &test_card && references == 1 && mounts == 1);
    assert(candis_storage_unmount() == ESP_OK && references == 0 && unmounts == 1);
#endif
    assert(candis_storage_detect_stop() == ESP_OK && references == 0 && !detect_enabled);

    assert(bsp_sdcard_mount() == ESP_OK && references == 1 && mounts == 2);
    assert(bsp_sdcard_get_handle() == &test_card);
    assert(candis_storage_unmount() == ESP_ERR_INVALID_STATE);
    assert(bsp_sdcard_unmount() == ESP_OK && references == 0 && unmounts == 2);

    expected_cd = OTHER_CD;
    config = config_for(OTHER_CD, false);
    assert(candis_storage_configure(&config) == ESP_OK);
    assert(candis_storage_detect_start(false) == ESP_OK && references == 0 && detect_enabled);
    assert(candis_storage_mount("/other", false, &card) == ESP_OK && mounts == 3);
    assert(candis_storage_unmount() == ESP_OK && references == 0 && unmounts == 3);
    assert(candis_storage_detect_stop() == ESP_OK && !detect_enabled);
    candis_storage_reset_config();

    const esp_vfs_fat_sdmmc_mount_config_t format_mount = {.max_files = 1, .format_if_mount_failed = true};
    bsp_sdcard_cfg_t bsp = {.mount = &format_mount};
    assert(candis_storage_configure(&config) == ESP_ERR_INVALID_ARG || bsp_sdcard_sdmmc_mount(&bsp) != ESP_OK);
    (void)resets;
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
static bool removed;
static unsigned syncs;

esp_err_t candis_storage_check_card(void)
{
    if (!present) {
        removed = true;
        present = true;
    }
    return removed ? ESP_ERR_NOT_FOUND : ESP_OK;
}

ssize_t __real_write(int fd, const void *data, size_t size);
ssize_t __wrap_write(int fd, const void *data, size_t size)
{
    ssize_t result = __real_write(fd, data, size);
    if (remove_during_write) present = false;
    return result;
}
ssize_t __real_read(int fd, void *data, size_t size);
ssize_t __wrap_read(int fd, void *data, size_t size)
{
    ssize_t result = __real_read(fd, data, size);
    if (corrupt_read && result > 0) ((unsigned char *)data)[0] ^= 1;
    return result;
}
int __real_fsync(int fd);
int __wrap_fsync(int fd) { ++syncs; return fail_sync ? -1 : __real_fsync(fd); }

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
    def test_configurable_card_detect_mount_and_cleanup(self):
        component = Path(__file__).resolve().parents[1] / "components/candis"
        idf = Path(os.environ["IDF_PATH"]) / "components/esp_common/include"
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
                 f"-I{idf}", f"-I{component / 'include'}", str(driver), str(test), "-o", str(binary)],
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
                '#include "esp_err.h"\nesp_err_t candis_storage_check_card(void);\n')
            (root / "esp_check.h").write_text(
                '#define ESP_RETURN_ON_ERROR(expr, tag, ...) do { (void)(tag); '
                'esp_err_t code = (expr); if (code != ESP_OK) return code; } while (0)\n'
                '#define ESP_RETURN_ON_FALSE(cond, err, tag, ...) do { (void)(tag); '
                'if (!(cond)) return err; } while (0)\n'
                '#define ESP_LOGI(tag, ...) ((void)(tag))\n')
            medium = root / "card"
            medium.mkdir()
            header = (example / "include/tf_verify.h").read_text()
            (root / "tf_verify.h").write_text(header.replace('"/sdcard"', f'"{medium}"'))
            shutil.copyfile(example / "tf_verify.c", root / "tf_verify.c")
            (root / "test.c").write_text(FILE_TEST)
            binary = root / "test"
            subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", f"-I{root}", f"-I{idf}",
                 "-Wl,--wrap=read,--wrap=write,--wrap=fsync", str(root / "tf_verify.c"),
                 str(root / "test.c"), "-o", str(binary)], check=True)
            subprocess.run([binary], check=True)
