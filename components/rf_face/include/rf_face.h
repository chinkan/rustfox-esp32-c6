// SPDX-License-Identifier: MIT
// Copyright (c) 2026 chinkan.ai
//
// Client-side fox emotion and blink. Names overlap the MIT lvgl_kawaii_face
// vocabulary (neutral, happy, sad, surprised, angry, sleepy, worried). Blink
// is a timer overlay, same idea as that widget's auto-blink, not a separate
// emotion. The LVGL widget is vendored but not linked: see third_party/.

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RF_FACE_W 96
#define RF_FACE_H 96

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

typedef struct {
    rf_emotion_t emotion;
    uint8_t eye_open; /* 0 closed .. 100 open, after blink */
    uint8_t blinking;
    uint32_t blink_interval_ms;
    uint32_t blink_hold_ms;
    uint32_t ms_since_blink;
    uint32_t blink_elapsed;
} rf_face_t;

void rf_face_init(rf_face_t *face);
void rf_face_set_emotion(rf_face_t *face, rf_emotion_t emotion);
/* Advance blink. Returns 1 when eye_open or emotion changed. */
int rf_face_tick(rf_face_t *face, uint32_t elapsed_ms);

rf_emotion_t rf_face_emotion_from_text(const char *text);
const char *rf_face_emotion_name(rf_emotion_t emotion);

/* Multi-line status plus a text fox. Returns 0 when the whole picture fit. */
int rf_face_format(const rf_face_t *face, char *dst, size_t dst_len);

/* Packed RGB565, row-major. Returns 0, or RF-style -1 on bad args. */
int rf_face_render_rgb565(const rf_face_t *face, uint16_t *dst, int width, int height);

#ifdef __cplusplus
}
#endif
