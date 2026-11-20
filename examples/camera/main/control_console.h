/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>

void camera_control_console_help(void);
/** Poll after returning the previous frame. False requests clean preview shutdown. */
bool camera_control_console_poll(void);
