// SPDX-License-Identifier: MIT
// Copyright (c) 2026 chinkan.ai

#include "rf_talk.h"

#include <string.h>

void rf_talk_init(rf_talk_t *talk) {
    if (!talk) {
        return;
    }
    memset(talk, 0, sizeof(*talk));
}

void rf_talk_set_link(rf_talk_t *talk, int up) {
    if (!talk) {
        return;
    }
    talk->link_up = up ? 1 : 0;
    if (!talk->link_up && talk->phase == RF_TALK_HOLDING) {
        talk->phase = RF_TALK_DISCARD;
        talk->cap_cue = 0;
    }
}

void rf_talk_set_playing(rf_talk_t *talk, int playing) {
    if (!talk) {
        return;
    }
    talk->playing = playing ? 1 : 0;
}

void rf_talk_press(rf_talk_t *talk) {
    if (!talk || talk->phase != RF_TALK_IDLE) {
        return;
    }
    talk->barge_in = talk->playing ? 1 : 0;
    talk->playing = 0;
    talk->cap_cue = 0;
    talk->held_ms = 0;
    if (!talk->link_up) {
        talk->phase = RF_TALK_DISCARD;
        return;
    }
    talk->phase = RF_TALK_HOLDING;
}

void rf_talk_tick(rf_talk_t *talk, uint32_t elapsed_ms) {
    if (!talk || talk->phase != RF_TALK_HOLDING) {
        return;
    }
    if (elapsed_ms > RF_TALK_MAX_HOLD_MS) {
        elapsed_ms = RF_TALK_MAX_HOLD_MS;
    }
    if (talk->held_ms > RF_TALK_MAX_HOLD_MS - elapsed_ms) {
        talk->held_ms = RF_TALK_MAX_HOLD_MS;
    } else {
        talk->held_ms += elapsed_ms;
    }
    if (talk->held_ms >= RF_TALK_MAX_HOLD_MS) {
        talk->phase = RF_TALK_SEND;
        talk->cap_cue = 1;
    }
}

void rf_talk_release(rf_talk_t *talk) {
    if (!talk || talk->phase != RF_TALK_HOLDING) {
        return;
    }
    if (talk->held_ms < RF_TALK_MIN_HOLD_MS) {
        talk->phase = RF_TALK_DISCARD;
        talk->cap_cue = 0;
        return;
    }
    talk->phase = RF_TALK_SEND;
}

void rf_talk_finish(rf_talk_t *talk) {
    if (!talk) {
        return;
    }
    talk->phase = RF_TALK_IDLE;
    talk->held_ms = 0;
    talk->cap_cue = 0;
    talk->barge_in = 0;
}

int rf_cst_frame_down(const uint8_t *data, size_t len) {
    if (!data || len < 7) {
        return 0;
    }
    if (data[6] != RF_CST_ACK) {
        return 0;
    }
    return (data[5] & 0x7F) > 0;
}

int rf_cst_kind_from_id(uint16_t chip_id) {
    if (chip_id == RF_CST_CHIP_CST9220) {
        return 1;
    }
    if (chip_id == RF_CST_CHIP_CST9217) {
        return 2;
    }
    return 0;
}

static void wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

void rf_wav_write_header(uint8_t dst[RF_WAV_HEADER_BYTES], uint32_t sample_rate, uint32_t pcm_bytes) {
    if (!dst) {
        return;
    }
    memset(dst, 0, RF_WAV_HEADER_BYTES);
    uint16_t channels = 1;
    uint16_t bits = 16;
    uint32_t byte_rate = sample_rate * channels * (bits / 8);
    uint16_t block = (uint16_t)(channels * (bits / 8));
    memcpy(dst, "RIFF", 4);
    wr32(dst + 4, 36u + pcm_bytes);
    memcpy(dst + 8, "WAVE", 4);
    memcpy(dst + 12, "fmt ", 4);
    wr32(dst + 16, 16);
    wr16(dst + 20, 1);
    wr16(dst + 22, channels);
    wr32(dst + 24, sample_rate);
    wr32(dst + 28, byte_rate);
    wr16(dst + 32, block);
    wr16(dst + 34, bits);
    memcpy(dst + 36, "data", 4);
    wr32(dst + 40, pcm_bytes);
}

int rf_audio_origin_from_ws(const char *ws_uri, char *dst, size_t dst_len) {
    if (!ws_uri || !dst || dst_len < 8) {
        return -1;
    }
    const char *rest = NULL;
    const char *scheme = NULL;
    if (strncmp(ws_uri, "wss://", 6) == 0) {
        scheme = "https://";
        rest = ws_uri + 6;
    } else if (strncmp(ws_uri, "ws://", 5) == 0) {
        scheme = "http://";
        rest = ws_uri + 5;
    } else {
        return -1;
    }
    if (!rest[0] || rest[0] == '/') {
        return -1;
    }
    const char *slash = strchr(rest, '/');
    size_t host_len = slash ? (size_t)(slash - rest) : strlen(rest);
    if (host_len == 0) {
        return -1;
    }
    size_t scheme_len = strlen(scheme);
    if (scheme_len + host_len + 1 > dst_len) {
        return -1;
    }
    memcpy(dst, scheme, scheme_len);
    memcpy(dst + scheme_len, rest, host_len);
    dst[scheme_len + host_len] = '\0';
    return 0;
}

int rf_audio_path_ok(const char *url) {
    static const char prefix[] = "/api/device/v1/audio/";
    if (!url || strncmp(url, prefix, sizeof(prefix) - 1) != 0) {
        return 0;
    }
    if (strstr(url, "://") != NULL) {
        return 0;
    }
    if (url[sizeof(prefix) - 1] == '\0') {
        return 0;
    }
    return 1;
}
