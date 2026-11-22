/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>

#include "candis_camera_profile_internal.h"

static const candis_camera_profile_t *const s_profiles[] = {
    &candis_camera_profile_bf20a6,
    &candis_camera_profile_bf3031,
    &candis_camera_profile_bf3045,
    &candis_camera_profile_bf3925,
    &candis_camera_profile_bf3a03,
    &candis_camera_profile_gc0308,
    &candis_camera_profile_gc032a,
    &candis_camera_profile_gc2145,
    &candis_camera_profile_mt9d111,
    &candis_camera_profile_ov2640,
    &candis_camera_profile_ov3640,
    &candis_camera_profile_ov3660,
    &candis_camera_profile_ov5640,
    &candis_camera_profile_sc030iot,
    &candis_camera_profile_sc101iot,
    &candis_camera_profile_sc2336,
    &candis_camera_profile_sp0a39,
};
_Static_assert(sizeof(s_profiles) / sizeof(s_profiles[0]) == CANDIS_CAMERA_SENSOR_COUNT,
               "missing camera profile");

size_t candis_camera_profile_count(void)
{
    return sizeof(s_profiles) / sizeof(s_profiles[0]);
}

const candis_camera_profile_t *candis_camera_profile_at(size_t index)
{
    return index < candis_camera_profile_count() ? s_profiles[index] : NULL;
}

const candis_camera_profile_t *candis_camera_profile_get(candis_camera_sensor_t sensor)
{
    return sensor >= 0 && sensor < CANDIS_CAMERA_SENSOR_COUNT ? s_profiles[sensor] : NULL;
}

const char *candis_camera_sensor_name(candis_camera_sensor_t sensor)
{
    const candis_camera_profile_t *profile = candis_camera_profile_get(sensor);
    return profile ? profile->model : NULL;
}

esp_err_t candis_camera_sensor_from_name(const char *name, candis_camera_sensor_t *sensor)
{
    if (!name || !sensor) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < candis_camera_profile_count(); ++i) {
        if (strcmp(name, s_profiles[i]->model) == 0) {
            *sensor = s_profiles[i]->sensor;
            return ESP_OK;
        }
    }
    *sensor = CANDIS_CAMERA_SENSOR_UNKNOWN;
    return ESP_ERR_NOT_FOUND;
}

bool candis_camera_profile_matches_id(const candis_camera_profile_t *profile,
                                      const esp_cam_sensor_id_t *id)
{
    if (!profile || !id) {
        return false;
    }
    for (unsigned i = 0; i < profile->chip_id_count &&
         i < sizeof(profile->chip_ids) / sizeof(profile->chip_ids[0]); ++i) {
        if (profile->chip_ids[i] == id->pid) {
            return true;
        }
    }
    return false;
}

const candis_camera_profile_t *candis_camera_profile_find_id(const esp_cam_sensor_id_t *id)
{
    for (size_t i = 0; id && i < candis_camera_profile_count(); ++i) {
        if (candis_camera_profile_matches_id(s_profiles[i], id)) {
            return s_profiles[i];
        }
    }
    return NULL;
}

static uint32_t control_flag(candis_camera_control_id_t control)
{
    switch (control) {
    case CANDIS_CAMERA_CONTROL_GAIN:
        return CANDIS_CAMERA_PROFILE_CONTROL_GAIN;
    case CANDIS_CAMERA_CONTROL_EXPOSURE:
        return CANDIS_CAMERA_PROFILE_CONTROL_EXPOSURE;
    case CANDIS_CAMERA_CONTROL_HFLIP:
    case CANDIS_CAMERA_CONTROL_VFLIP:
        return CANDIS_CAMERA_PROFILE_CONTROL_FLIP;
    case CANDIS_CAMERA_CONTROL_AE_LEVEL:
        return CANDIS_CAMERA_PROFILE_CONTROL_AE_LEVEL;
    case CANDIS_CAMERA_CONTROL_EXPOSURE_100US:
        return CANDIS_CAMERA_PROFILE_CONTROL_EXPOSURE_100US;
    case CANDIS_CAMERA_CONTROL_JPEG_QUALITY:
        return CANDIS_CAMERA_PROFILE_CONTROL_JPEG_QUALITY;
    case CANDIS_CAMERA_CONTROL_TEST_PATTERN:
        return CANDIS_CAMERA_PROFILE_CONTROL_TEST_PATTERN;
    default:
        return 0;
    }
}

bool candis_camera_profile_supports_control(const candis_camera_profile_t *profile, candis_camera_control_id_t control)
{
    return profile && (profile->control_flags & control_flag(control));
}

bool candis_camera_profile_can_read_control(const candis_camera_profile_t *profile, candis_camera_control_id_t control)
{
    return profile && (profile->readable_control_flags & control_flag(control));
}
