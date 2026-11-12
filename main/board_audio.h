// SPDX-License-Identifier: MIT
// Copyright (c) 2026 chinkan.ai

#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

/* ES7210 capture and ES8311 playback on the shared I2S pins, 16 kHz. */
esp_err_t board_audio_start(i2c_master_bus_handle_t bus);

/* Mono s16le. frames is the number of samples, at most 256. */
esp_err_t board_audio_read_mono(int16_t *dst, size_t frames);
esp_err_t board_audio_write_mono(const int16_t *src, size_t frames);

/* Drop samples already queued for the speaker. */
void board_audio_stop_playback(void);
