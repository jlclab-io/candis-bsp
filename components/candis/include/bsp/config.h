/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "sdkconfig.h"

#define BSP_BOARD_CANDIS

#ifndef BSP_CONFIG_NO_GRAPHIC_LIB
#ifdef CONFIG_CANDIS_NO_GRAPHIC_LIB
#define BSP_CONFIG_NO_GRAPHIC_LIB (1)
#else
#define BSP_CONFIG_NO_GRAPHIC_LIB (0)
#endif
#endif

/* These flags describe interfaces physically supplied by the Candis core
 * board or enabled by this build. Optional baseboards are runtime state. */
#define BSP_CAPS_DISPLAY       1
#define BSP_CAPS_TOUCH         1
#define BSP_CAPS_SDCARD        0
#define BSP_CAPS_BAT           1
#define BSP_CAPS_POWER         1
#define BSP_CAPS_MOTOR         0
#define BSP_CAPS_BUTTONS       0
#define BSP_CAPS_KNOB          0
#define BSP_CAPS_LED           0
#define BSP_CAPS_CAMERA        0
#define BSP_CAPS_HUMITURE      0
#define BSP_CAPS_EXPANSION     1

#ifdef CONFIG_CANDIS_AUDIO
#define BSP_CAPS_AUDIO         1
#else
#define BSP_CAPS_AUDIO         0
#endif
#define BSP_CAPS_AUDIO_SPEAKER BSP_CAPS_AUDIO
#define BSP_CAPS_AUDIO_MIC     BSP_CAPS_AUDIO

/* BMI270 additionally requires expansion hardware at runtime. */
#ifdef CONFIG_CANDIS_IMU
#define BSP_CAPS_IMU           1
#else
#define BSP_CAPS_IMU           0
#endif

#ifdef CONFIG_CANDIS_USB_DEVICE
#define BSP_CAPS_USB_DEVICE    1
#else
#define BSP_CAPS_USB_DEVICE    0
#endif
#ifdef CONFIG_CANDIS_USB_HOST
#define BSP_CAPS_USB_HOST      1
#else
#define BSP_CAPS_USB_HOST      0
#endif
#define BSP_CAPS_USB           (BSP_CAPS_USB_DEVICE || BSP_CAPS_USB_HOST)
