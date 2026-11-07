// SPDX-License-Identifier: MIT
// Copyright (c) 2026 chinkan.ai

#pragma once

#include "rf_face.h"

#include "esp_err.h"

/* Power the AXP2101 rails and the QSPI panel, then paint black.
 * Failure leaves the socket path usable. */
esp_err_t board_panel_init(void);

/* Blit the 96x96 fox in the center. No full-frame framebuffer. */
void board_panel_draw(const rf_face_t *face);
