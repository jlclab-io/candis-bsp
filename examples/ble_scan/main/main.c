/* SPDX-License-Identifier: Apache-2.0 */
#include <limits.h>
#include <string.h>

#include "candis_example_usb.h"
#include "esp_log.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"

static const char *TAG = "candis_ble_scan";
enum { SCAN_MS = 5000, REPEAT_MS = 2000 };

static unsigned s_reports;
static int s_best_rssi;
static struct ble_npl_callout s_repeat;
static bool s_track_peer;
static ble_uuid128_t s_peer_uuid;
static unsigned s_peer_reports;
static int s_peer_rssi;

static void collect_peer(const struct ble_gap_disc_desc *report)
{
    struct ble_hs_adv_fields fields;
    if (!s_track_peer || ble_hs_adv_parse_fields(&fields, report->data, report->length_data) != 0 ||
        fields.svc_data_uuid128_len < sizeof(s_peer_uuid.value) ||
        memcmp(fields.svc_data_uuid128, s_peer_uuid.value, sizeof(s_peer_uuid.value)) != 0) {
        return;
    }
    ++s_peer_reports;
    if (report->rssi != 127 && report->rssi > s_peer_rssi) {
        s_peer_rssi = report->rssi;
    }
}

static void report_peer(void)
{
    if (!s_track_peer) {
        return;
    }
    if (s_peer_rssi < -127) {
        ESP_LOGI(TAG, "BLE_PEER uuid=%s reports=%u rssi=unavailable",
                 CONFIG_CANDIS_BLE_SCAN_PEER_UUID, s_peer_reports);
    } else {
        ESP_LOGI(TAG, "BLE_PEER uuid=%s reports=%u rssi=%d dBm",
                 CONFIG_CANDIS_BLE_SCAN_PEER_UUID, s_peer_reports, s_peer_rssi);
    }
}

static int scan_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        ++s_reports;
        collect_peer(&event->disc);
        if (event->disc.rssi != 127 && event->disc.rssi > s_best_rssi) {
            s_best_rssi = event->disc.rssi;
        }
        break;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        report_peer();
        if (s_best_rssi == INT_MIN) {
            ESP_LOGI(TAG, "BLE_DONE reports=%u strongest=unavailable reason=%d",
                     s_reports, event->disc_complete.reason);
        } else {
            ESP_LOGI(TAG, "BLE_DONE reports=%u strongest=%d dBm reason=%d",
                     s_reports, s_best_rssi, event->disc_complete.reason);
        }
        ble_npl_callout_reset(&s_repeat, ble_npl_time_ms_to_ticks32(REPEAT_MS));
        break;
    default:
        break;
    }
    return 0;
}

static void start_scan(struct ble_npl_event *event)
{
    (void)event;
    uint8_t address_type;
    int rc = ble_hs_id_infer_auto(0, &address_type);
    if (rc == 0) {
        const struct ble_gap_disc_params params = {
            .passive = 1,
            .filter_duplicates = 1,
        };
        s_best_rssi = INT_MIN;
        s_reports = 0;
        s_peer_reports = 0;
        s_peer_rssi = INT_MIN;
        rc = ble_gap_disc(address_type, SCAN_MS, &params, scan_event, NULL);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "BLE_SCAN_ERROR rc=%d", rc);
        ble_npl_callout_reset(&s_repeat, ble_npl_time_ms_to_ticks32(REPEAT_MS));
    }
}

static void on_sync(void)
{
    ESP_LOGI(TAG, "BLE_READY passive windows=%d ms", SCAN_MS);
    start_scan(NULL);
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void app_main(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(candis_example_usb_start(CANDIS_EXAMPLE_USB_CONSOLE, true));
    const esp_err_t nvs = nvs_flash_init();
    if (nvs != ESP_OK) {
        ESP_LOGE(TAG, "BLE_NVS %s", esp_err_to_name(nvs));
        return;
    }
    const char *peer = CONFIG_CANDIS_BLE_SCAN_PEER_UUID;
    if (*peer) {
        ble_uuid_any_t uuid;
        if (ble_uuid_from_str(&uuid, peer) != 0 || uuid.u.type != BLE_UUID_TYPE_128) {
            ESP_LOGE(TAG, "Expected a 128-bit peer UUID");
            return;
        }
        s_peer_uuid = uuid.u128;
        s_track_peer = true;
    }
    ESP_ERROR_CHECK(nimble_port_init());
    ble_npl_callout_init(&s_repeat, nimble_port_get_dflt_eventq(), start_scan, NULL);
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(host_task);
}
