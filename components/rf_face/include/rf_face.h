// SPDX-License-Identifier: MIT
// Copyright (c) 2026 chinkan.ai
//
// Full-panel procedural fox. Ears, head, eyes, nose, and mouth are
// parameterized. Expressions move with smoothstep. The panel blits this
// in 16-line stripes; this component never allocates a 480x480 buffer
// and does not draw a logo.

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RF_FACE_W 96
#define RF_FACE_H 96
#define RF_FACE_LERP_MS 240

#define RF_FACE_RGB565(r, g, b) \
    ((uint16_t)((((uint16_t)(r) & 0xF8) << 8) | (((uint16_t)(g) & 0xFC) << 3) | \
                ((uint16_t)(b) >> 3)))

#define RF_FACE_COLOR_BG RF_FACE_RGB565(26, 18, 12)
#define RF_FACE_COLOR_FUR RF_FACE_RGB565(230, 120, 40)
#define RF_FACE_COLOR_EAR RF_FACE_RGB565(196, 84, 24)
#define RF_FACE_COLOR_INNER_EAR RF_FACE_RGB565(244, 164, 140)
#define RF_FACE_COLOR_SNOUT RF_FACE_RGB565(255, 214, 170)
#define RF_FACE_COLOR_SCLERA RF_FACE_RGB565(255, 250, 245)
#define RF_FACE_COLOR_PUPIL RF_FACE_RGB565(24, 16, 16)
#define RF_FACE_COLOR_NOSE RF_FACE_RGB565(90, 36, 28)
#define RF_FACE_COLOR_MOUTH RF_FACE_RGB565(120, 40, 36)
#define RF_FACE_COLOR_CUE RF_FACE_RGB565(255, 196, 64)
#define RF_FACE_COLOR_ALERT RF_FACE_RGB565(176, 48, 32)

typedef enum {
    RF_EMOTION_NEUTRAL = 0,
    RF_EMOTION_HAPPY,
    RF_EMOTION_SAD,
    RF_EMOTION_SURPRISED,
    RF_EMOTION_WORRIED,
    RF_EMOTION_SLEEPY,
    RF_EMOTION_THINKING,
    RF_EMOTION_ANGRY,
    RF_EMOTION_COUNT
} rf_emotion_t;

/* Press-and-hold and playback cues. Worried still wins on the face. */
typedef enum {
    RF_CUE_NONE = 0,
    RF_CUE_LISTENING,
    RF_CUE_SPEAKING,
    RF_CUE_HOLD_CAP,
    RF_CUE_COUNT
} rf_cue_t;

typedef struct {
    rf_emotion_t emotion;
    rf_cue_t cue;
    uint8_t eye_open; /* 0 closed .. 100 open, after blink */
    uint8_t blinking;
    uint32_t blink_interval_ms;
    uint32_t blink_hold_ms;
    uint32_t ms_since_blink;
    uint32_t blink_elapsed;
    uint32_t anim_ms;
    uint32_t lerp_ms;
    int16_t eye_q; /* displayed 0..1000, before blink */
    int16_t look_q;
    int16_t smile_q;
    int16_t mouth_q;
    int16_t worry_q;
    int16_t perk_q;
    int16_t eye_from, eye_to;
    int16_t look_from, look_to;
    int16_t smile_from, smile_to;
    int16_t mouth_from, mouth_to;
    int16_t worry_from, worry_to;
    int16_t perk_from, perk_to;
    int16_t sway_bucket;
} rf_face_t;

void rf_face_init(rf_face_t *face);
void rf_face_set_emotion(rf_face_t *face, rf_emotion_t emotion);
void rf_face_set_cue(rf_face_t *face, rf_cue_t cue);
/* Advance blink, idle motion, and expression lerp.
 * Returns 1 when the picture changed. */
int rf_face_tick(rf_face_t *face, uint32_t elapsed_ms);

rf_emotion_t rf_face_emotion_from_text(const char *text);
const char *rf_face_emotion_name(rf_emotion_t emotion);

/* Multi-line status plus a text fox. Returns 0 when the whole picture fit. */
int rf_face_format(const rf_face_t *face, char *dst, size_t dst_len);

/* Packed RGB565, row-major, for the whole image. Host tests use this.
 * The device path uses the stripe call and does not keep this buffer. */
int rf_face_render_rgb565(const rf_face_t *face, uint16_t *dst, int width, int height);

/* Fill `rows` lines starting at absolute y0. dst holds rows * width pixels.
 * panel_h is the full panel height (480 on device). */
int rf_face_render_stripe(const rf_face_t *face, uint16_t *dst, int width, int y0, int rows,
                          int panel_h);

#ifdef __cplusplus
}
#endif
