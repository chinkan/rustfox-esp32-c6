// SPDX-License-Identifier: MIT
// Copyright (c) 2026 chinkan.ai

#pragma once

#include "esp_err.h"

/* Probe CST9220 and CST9217 on the shared I2C bus. Calls on_contact(1/0)
 * from a task when the finger state changes. */
typedef void (*board_touch_fn)(int down);
esp_err_t board_touch_start(board_touch_fn on_contact);
