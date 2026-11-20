/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

static inline unsigned camera_clamp_channel(int value)
{
    return value < 0 ? 0 : value > 255 ? 255 : (unsigned)value;
}

/* BT.601 Y'CbCr to native RGB565. The coefficients are scaled by 256. */
static inline uint16_t camera_yuv_to_rgb565(int y, int u, int v, bool limited)
{
    u -= 128;
    v -= 128;
    const int luma = limited ? 298 * (y - 16) : 256 * y;
    const unsigned red = camera_clamp_channel((luma + (limited ? 409 : 359) * v + 128) / 256);
    const unsigned green = camera_clamp_channel((luma - (limited ? 100 : 88) * u -
                                                    (limited ? 208 : 183) * v + 128) / 256);
    const unsigned blue = camera_clamp_channel((luma + (limited ? 516 : 454) * u + 128) / 256);
    return ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3);
}

/* Each packed 4:2:2 pair shares U and V while keeping separate luma values. */
static inline uint16_t camera_yuv_pixel(const uint8_t *pair, bool yuyv, unsigned pixel, bool limited)
{
    const unsigned chroma = yuyv ? 1 : 0;
    return camera_yuv_to_rgb565(pair[2 * pixel + (1 - chroma)], pair[chroma], pair[chroma + 2], limited);
}
