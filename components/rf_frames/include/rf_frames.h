// SPDX-License-Identifier: MIT
// Copyright (c) 2026 chinkan.ai
//
// ADR-0056 Device WebSocket JSON text frames.
// Contract: docs/device-ws.md (matches chinkan/RustFox src/device_ws.rs).

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RF_WS_PATH "/api/device/v1/ws"
#define RF_MAX_USER_TEXT_CHARS 16000
#define RF_AUTH_WAIT_MS 15000
#define RF_META_MAX_CHARS 64
#define RF_FRAME_TEXT_CAP 1024

#define RF_OK 0
#define RF_ERR_ARG (-1)
#define RF_ERR_NOSPACE (-2)
#define RF_ERR_BAD_FRAME (-3)
#define RF_ERR_RANGE (-4)

typedef enum {
    RF_TYPE_NONE = 0,
    RF_TYPE_AUTH,
    RF_TYPE_USER_TEXT,
    RF_TYPE_PING,
    RF_TYPE_DEVICE_HELLO,
    RF_TYPE_ASSISTANT_TEXT,
    RF_TYPE_DEVICE_ACK,
    RF_TYPE_PONG,
    RF_TYPE_ERROR,
    RF_TYPE_ASSISTANT_AUDIO,
    RF_TYPE_UNKNOWN
} rf_frame_type_t;

typedef struct {
    rf_frame_type_t type;
    char type_name[32];
    char token[128];
    char text[RF_FRAME_TEXT_CAP];
    int text_truncated;
    char code[32];
    char message[160];
    char status[32];
    char device_id[RF_META_MAX_CHARS + 1];
    char room[RF_META_MAX_CHARS + 1];
    char fw_version[32];
    /* assistant_audio (ADR-0066). sample_rate and channels are 0 when absent. */
    char audio_id[48];
    char audio_url[160];
    char audio_encoding[16];
    int sample_rate;
    int channels;
    int audio_truncated;
} rf_frame_t;

/* Encoders return the JSON byte length (no trailing NUL) or a negative RF_ERR_*. */
int rf_encode_ping(char *dst, size_t dst_len);
int rf_encode_auth(char *dst, size_t dst_len, const char *token);
int rf_encode_user_text(char *dst, size_t dst_len, const char *text);
int rf_encode_device_hello(char *dst, size_t dst_len, const char *device_id,
                           const char *room, const char *fw_version);

/* Parse one JSON object. Copies string fields. text longer than the cap is
 * truncated and text_truncated is set. Returns RF_OK or RF_ERR_*. */
int rf_parse_frame(const char *json, size_t len, rf_frame_t *out);

const char *rf_frame_type_name(rf_frame_type_t type);

/* 16 kHz mono s16le assistant_audio with a same-origin audio path. */
int rf_frame_audio_playable(const rf_frame_t *frame);

#ifdef __cplusplus
}
#endif
