// Copyright 2015-2020 The Apache Software Foundation
// Modifications Copyright 2017-2020 Espressif Systems (Shanghai) CO., LTD.
//
// Portions of this software were developed at Runtime Inc, copyright 2015.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "bleprph.h"
#include "esp_log.h"


static const ble_uuid16_t gatt_svr_svc_uuid = BLE_UUID16_INIT(CANDIS_GATT_SVC_UUID);
static const ble_uuid16_t gatt_svr_chr_data_uuid = BLE_UUID16_INIT(CANDIS_GATT_DATA_UUID);

static const char *TAG = "candis_coex_gatt";

volatile bool example_ble_connected;
volatile bool example_ble_data_received;
volatile uint32_t example_ble_write_count;

static uint8_t gatt_svr_data_val[32] = "CANDIS_COEX_READY";
static uint16_t gatt_svr_data_len = sizeof("CANDIS_COEX_READY") - 1;

static int
gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                    struct ble_gatt_access_ctxt *ctxt,
                    void *arg);

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        /*** Service: Candis coexistence example. */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svr_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[])
        { {
                /*** Characteristic: Static value. */
                .uuid = &gatt_svr_chr_data_uuid.u,
                .access_cb = gatt_svr_chr_access,
                .flags = BLE_GATT_CHR_F_READ |
                BLE_GATT_CHR_F_WRITE
            }, {
                0, /* No more characteristics in this service. */
            }
        },
    },

    {
        0, /* No more services. */
    },
};

static int
gatt_svr_chr_write(struct os_mbuf *om, uint16_t min_len, uint16_t max_len,
                   void *dst, uint16_t *len)
{
    uint16_t om_len;
    int rc;

    om_len = OS_MBUF_PKTLEN(om);
    if (om_len < min_len || om_len > max_len) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    rc = ble_hs_mbuf_to_flat(om, dst, max_len, len);
    if (rc != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    return 0;
}

static int
gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                    struct ble_gatt_access_ctxt *ctxt,
                    void *arg)
{
    const ble_uuid_t *uuid;
    int rc;

    uuid = ctxt->chr->uuid;

    /* Determine which characteristic is being accessed by examining its
     * 128-bit UUID.
     */

    if (ble_uuid_cmp(uuid, &gatt_svr_chr_data_uuid.u) == 0) {
        switch (ctxt->op) {
        case BLE_GATT_ACCESS_OP_READ_CHR:
            rc = os_mbuf_append(ctxt->om, gatt_svr_data_val, gatt_svr_data_len);
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;

        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            rc = gatt_svr_chr_write(ctxt->om, 1, sizeof(gatt_svr_data_val),
                                    gatt_svr_data_val, &gatt_svr_data_len);
            if (rc == 0) {
                ++example_ble_write_count;
                example_ble_data_received = true;
                ESP_LOGI(TAG, "CANDIS_COEX_BLE_RX bytes=%u writes=%" PRIu32,
                         gatt_svr_data_len, example_ble_write_count);
            }
            return rc;

        default:
            assert(0);
            return BLE_ATT_ERR_UNLIKELY;
        }
    }

    /* Unknown characteristic; the nimble stack should not have called this
     * function.
     */
    assert(0);
    return BLE_ATT_ERR_UNLIKELY;
}

void
gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGI(TAG, "registered service %s with handle=%d",
                    ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                    ctxt->svc.handle);
        break;

    case BLE_GATT_REGISTER_OP_CHR:
        ESP_LOGI(TAG, "registered characteristic %s with "
                    "def_handle=%d val_handle=%d",
                    ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                    ctxt->chr.def_handle,
                    ctxt->chr.val_handle);
        break;

    case BLE_GATT_REGISTER_OP_DSC:
        ESP_LOGD(TAG, "registering descriptor %s with handle=%d",
                    ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                    ctxt->dsc.handle);
        break;

    default:
        assert(0);
        break;
    }
}

int
gatt_svr_init(void)
{
    int rc;

#if CONFIG_BT_NIMBLE_GAP_SERVICE
    ble_svc_gap_init();
#endif /* CONFIG_BT_NIMBLE_GAP_SERVICE */
#if MYNEWT_VAL(BLE_GATTS)
    ble_svc_gatt_init();
#endif
    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    return 0;
}
