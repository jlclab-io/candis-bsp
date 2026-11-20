/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "bsp/candis_camera.h"

/** Encode an 800x600 RGB565 still and save it to the validation fixture's TF card.
 * The camera must already be stopped. The aligned capture buffer is packed in place.
 */
esp_err_t camera_save_still_jpeg(const struct v4l2_pix_format *format, uint8_t *raw, bool swap);
