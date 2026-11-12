// SPDX-License-Identifier: MIT
// Copyright (c) 2026 chinkan.ai

#pragma once

#include "rf_face.h"

#include "driver/i2c_master.h"
#include "esp_err.h"

/* Shared I2C bus (AXP2101, touch, ES8311, ES7210). Safe to call twice. */
esp_err_t board_i2c_init(void);
i2c_master_bus_handle_t board_i2c_bus(void);

/* Power the AXP2101 rails and the QSPI panel, then paint black.
 * Failure leaves the socket path usable. The SH8601 command set stays. */
esp_err_t board_panel_init(void);

/* Blit the full 480x480 fox in STRIPE_ROWS (16) line bands.
 * There is no 480x480 framebuffer. */
void board_panel_draw(const rf_face_t *face);
