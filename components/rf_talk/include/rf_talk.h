// SPDX-License-Identifier: MIT
// Copyright (c) 2026 chinkan.ai
//
// Press-and-hold decisions, CST92xx report checks, WAV header, and the
// HTTP origin derived from the Device WebSocket URI. No sockets here.

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RF_TALK_MIN_HOLD_MS 300
#define RF_TALK_MAX_HOLD_MS 15000
#define RF_AUDIO_RATE 16000
#define RF_WAV_HEADER_BYTES 44
#define RF_AUDIO_POST_PATH "/api/device/v1/audio"
#define RF_CST_ACK 0xAB
#define RF_CST_CHIP_CST9217 0x9217
#define RF_CST_CHIP_CST9220 0x9220

typedef enum {
    RF_TALK_IDLE = 0,
    RF_TALK_HOLDING,
    RF_TALK_SEND,
    RF_TALK_DISCARD
} rf_talk_phase_t;

typedef struct {
    rf_talk_phase_t phase;
    uint32_t held_ms;
    int link_up;
    int playing;
    int cap_cue;
    int barge_in;
} rf_talk_t;

void rf_talk_init(rf_talk_t *talk);
void rf_talk_set_link(rf_talk_t *talk, int up);
void rf_talk_set_playing(rf_talk_t *talk, int playing);
/* Begin a hold. If the link is down, the phase is DISCARD. If playback
 * was active, barge_in is set and playing is cleared. */
void rf_talk_press(rf_talk_t *talk);
void rf_talk_tick(rf_talk_t *talk, uint32_t elapsed_ms);
void rf_talk_release(rf_talk_t *talk);
/* After the caller has uploaded or dropped the clip. */
void rf_talk_finish(rf_talk_t *talk);

/* 1 when a CST92xx report has the ack marker and at least one point. */
int rf_cst_frame_down(const uint8_t *data, size_t len);
/* 1 for CST9220, 2 for CST9217, 0 for anything else. */
int rf_cst_kind_from_id(uint16_t chip_id);

void rf_wav_write_header(uint8_t dst[RF_WAV_HEADER_BYTES], uint32_t sample_rate,
                         uint32_t pcm_bytes);

/* ws://host[:port]/... -> http://host[:port], wss:// -> https://.
 * Returns 0, or -1. */
int rf_audio_origin_from_ws(const char *ws_uri, char *dst, size_t dst_len);

/* 1 when url is a device-audio path on this server (not a chunk index). */
int rf_audio_path_ok(const char *url);

#ifdef __cplusplus
}
#endif
