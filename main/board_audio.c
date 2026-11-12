// SPDX-License-Identifier: MIT
// Copyright (c) 2026 chinkan.ai
//
// Duplex I2S at 16 kHz. Both codecs are slaves of the ESP clock.
// The wire format is stereo so capture and playback share one BCLK/WS.
// Capture downmixes to mono. Playback duplicates mono onto both slots.
// This is not the Xiaozhi cloud or Opus path.

#include "board_audio.h"

#include "board_pins.h"

#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"

static const char *TAG = "rf_audio";

#define AUDIO_RATE 16000
#define AUDIO_FRAMES 256

static i2s_chan_handle_t s_tx;
static i2s_chan_handle_t s_rx;
static esp_codec_dev_handle_t s_play;
static esp_codec_dev_handle_t s_rec;
static int16_t s_stereo[AUDIO_FRAMES * 2];
static int s_ready;

void board_audio_stop_playback(void) {
    if (!s_tx) {
        return;
    }
    i2s_channel_disable(s_tx);
    i2s_channel_enable(s_tx);
}

esp_err_t board_audio_read_mono(int16_t *dst, size_t frames) {
    if (!s_ready || !dst || frames == 0 || frames > AUDIO_FRAMES) {
        return ESP_ERR_INVALID_ARG;
    }
    int rc = esp_codec_dev_read(s_rec, s_stereo, (int)(frames * 4));
    if (rc != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    for (size_t i = 0; i < frames; i++) {
        int32_t mixed = (int32_t)s_stereo[i * 2] + (int32_t)s_stereo[i * 2 + 1];
        dst[i] = (int16_t)(mixed / 2);
    }
    return ESP_OK;
}

esp_err_t board_audio_write_mono(const int16_t *src, size_t frames) {
    if (!s_ready || !src || frames == 0 || frames > AUDIO_FRAMES) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < frames; i++) {
        s_stereo[i * 2] = src[i];
        s_stereo[i * 2 + 1] = src[i];
    }
    int rc = esp_codec_dev_write(s_play, s_stereo, (int)(frames * 4));
    if (rc != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t board_audio_start(i2c_master_bus_handle_t bus) {
    if (!bus) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ready) {
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 512;
    esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx, &s_rx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s channel: %s", esp_err_to_name(err));
        return err;
    }

    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BOARD_I2S_MCLK,
            .bclk = BOARD_I2S_BCLK,
            .ws = BOARD_I2S_WS,
            .dout = BOARD_I2S_DOUT,
            .din = BOARD_I2S_DIN,
        },
    };
    err = i2s_channel_init_std_mode(s_tx, &std);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s tx: %s", esp_err_to_name(err));
        return err;
    }
    err = i2s_channel_init_std_mode(s_rx, &std);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s rx: %s", esp_err_to_name(err));
        return err;
    }
    err = i2s_channel_enable(s_tx);
    if (err != ESP_OK) {
        return err;
    }
    err = i2s_channel_enable(s_rx);
    if (err != ESP_OK) {
        return err;
    }

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .rx_handle = s_rx,
        .tx_handle = s_tx,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    if (!data_if || !gpio_if) {
        ESP_LOGE(TAG, "codec data if failed");
        return ESP_ERR_NO_MEM;
    }

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = I2C_NUM_0,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = bus,
        .clock_speed_hz = 400000,
    };
    const audio_codec_ctrl_if_t *out_ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);
    i2c_cfg.addr = ES7210_CODEC_DEFAULT_ADDR;
    const audio_codec_ctrl_if_t *in_ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!out_ctrl || !in_ctrl) {
        ESP_LOGE(TAG, "codec i2c failed");
        return ESP_FAIL;
    }

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = out_ctrl,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = -1,
        .use_mclk = true,
        .master_mode = false,
    };
    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = in_ctrl,
        .master_mode = false,
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2,
        .mclk_src = ES7210_MCLK_FROM_PAD,
    };
    const audio_codec_if_t *out_if = es8311_codec_new(&es8311_cfg);
    const audio_codec_if_t *in_if = es7210_codec_new(&es7210_cfg);
    if (!out_if || !in_if) {
        ESP_LOGE(TAG, "codec new failed");
        return ESP_FAIL;
    }

    esp_codec_dev_cfg_t play_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = out_if,
        .data_if = data_if,
    };
    esp_codec_dev_cfg_t rec_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = in_if,
        .data_if = data_if,
    };
    s_play = esp_codec_dev_new(&play_cfg);
    s_rec = esp_codec_dev_new(&rec_cfg);
    if (!s_play || !s_rec) {
        ESP_LOGE(TAG, "codec dev failed");
        return ESP_FAIL;
    }

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 2,
        .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0) | ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1),
        .sample_rate = AUDIO_RATE,
        .mclk_multiple = 0,
    };
    if (esp_codec_dev_open(s_play, &fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "ES8311 open failed");
        return ESP_FAIL;
    }
    if (esp_codec_dev_open(s_rec, &fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "ES7210 open failed");
        return ESP_FAIL;
    }
    esp_codec_dev_set_out_vol(s_play, 70);
    esp_codec_dev_set_in_gain(s_rec, 30.0f);
    s_ready = 1;
    ESP_LOGI(TAG, "ES7210 + ES8311 at %d Hz", AUDIO_RATE);
    return ESP_OK;
}
