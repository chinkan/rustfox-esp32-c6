// SPDX-License-Identifier: MIT
// Copyright (c) 2026 chinkan.ai

#include "rf_face.h"

#include <stdio.h>
#include <string.h>

static int contains_ci(const char *hay, const char *needle) {
    if (!hay || !needle || !needle[0]) {
        return 0;
    }
    size_t n = strlen(needle);
    for (const unsigned char *p = (const unsigned char *)hay; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] &&
               (unsigned char)((p[i] >= 'A' && p[i] <= 'Z') ? p[i] - 'A' + 'a' : p[i]) ==
                   (unsigned char)((needle[i] >= 'A' && needle[i] <= 'Z') ? needle[i] - 'A' + 'a'
                                                                           : needle[i])) {
            i++;
        }
        if (i == n) {
            return 1;
        }
    }
    return 0;
}

static int contains_any(const char *text, const char *const *needles) {
    for (size_t i = 0; needles[i]; i++) {
        const char *n = needles[i];
        int ascii = 1;
        for (const unsigned char *p = (const unsigned char *)n; *p; p++) {
            if (*p >= 0x80) {
                ascii = 0;
                break;
            }
        }
        if (ascii) {
            if (contains_ci(text, n)) {
                return 1;
            }
        } else if (strstr(text, n)) {
            return 1;
        }
    }
    return 0;
}

static uint8_t base_open(rf_emotion_t emotion) {
    switch (emotion) {
    case RF_EMOTION_SLEEPY:
        return 35;
    case RF_EMOTION_SAD:
        return 55;
    case RF_EMOTION_ANGRY:
        return 50;
    case RF_EMOTION_THINKING:
        return 70;
    case RF_EMOTION_WORRIED:
        return 80;
    case RF_EMOTION_HAPPY:
        return 90;
    case RF_EMOTION_SURPRISED:
    case RF_EMOTION_NEUTRAL:
    case RF_EMOTION_COUNT:
    default:
        return 100;
    }
}

void rf_face_init(rf_face_t *face) {
    if (!face) {
        return;
    }
    memset(face, 0, sizeof(*face));
    face->emotion = RF_EMOTION_SLEEPY;
    face->blink_interval_ms = 3200;
    face->blink_hold_ms = 140;
    face->eye_open = base_open(face->emotion);
}

void rf_face_set_emotion(rf_face_t *face, rf_emotion_t emotion) {
    if (!face || emotion >= RF_EMOTION_COUNT) {
        return;
    }
    face->emotion = emotion;
    if (!face->blinking) {
        face->eye_open = base_open(emotion);
    }
}

int rf_face_tick(rf_face_t *face, uint32_t elapsed_ms) {
    if (!face) {
        return 0;
    }
    if (face->blink_interval_ms == 0) {
        face->blink_interval_ms = 3200;
    }
    if (face->blink_hold_ms == 0) {
        face->blink_hold_ms = 140;
    }
    uint8_t prev_eye = face->eye_open;
    rf_emotion_t prev = face->emotion;
    uint32_t left = elapsed_ms;
    while (left > 0) {
        if (!face->blinking) {
            uint32_t need = face->blink_interval_ms - face->ms_since_blink;
            if (need == 0) {
                need = face->blink_interval_ms;
            }
            uint32_t step = left < need ? left : need;
            face->ms_since_blink += step;
            left -= step;
            if (face->ms_since_blink >= face->blink_interval_ms) {
                face->blinking = 1;
                face->blink_elapsed = 0;
                face->ms_since_blink = 0;
            }
        } else {
            uint32_t need = face->blink_hold_ms - face->blink_elapsed;
            if (need == 0) {
                need = face->blink_hold_ms;
            }
            uint32_t step = left < need ? left : need;
            face->blink_elapsed += step;
            left -= step;
            if (face->blink_elapsed >= face->blink_hold_ms) {
                face->blinking = 0;
                face->blink_elapsed = 0;
            }
        }
    }
    face->eye_open = face->blinking ? 0 : base_open(face->emotion);
    return face->eye_open != prev_eye || face->emotion != prev;
}

rf_emotion_t rf_face_emotion_from_text(const char *text) {
    if (!text || !text[0]) {
        return RF_EMOTION_NEUTRAL;
    }
    static const char *const sad[] = {"sorry", "sad", "cannot", "can't", "fail",
                                      "抱歉", "唔好意思", "不行", NULL};
    static const char *const worried[] = {"worry", "careful", "unsure", "小心", NULL};
    static const char *const surprised[] = {"wow", "surpris", "哇", NULL};
    static const char *const angry[] = {"angry", "生气", "生氣", NULL};
    static const char *const sleepy[] = {"sleep", "晚安", "瞓", NULL};
    static const char *const thinking[] = {"think", "let me", "我想想", NULL};
    static const char *const happy[] = {"happy", "great", "glad", "love", "done",
                                        "yes", "好的", "完成", "太好", NULL};
    if (contains_any(text, sad)) {
        return RF_EMOTION_SAD;
    }
    if (contains_any(text, worried)) {
        return RF_EMOTION_WORRIED;
    }
    if (contains_any(text, surprised)) {
        return RF_EMOTION_SURPRISED;
    }
    if (contains_any(text, angry)) {
        return RF_EMOTION_ANGRY;
    }
    if (contains_any(text, sleepy)) {
        return RF_EMOTION_SLEEPY;
    }
    if (contains_any(text, thinking)) {
        return RF_EMOTION_THINKING;
    }
    if (contains_any(text, happy)) {
        return RF_EMOTION_HAPPY;
    }
    return RF_EMOTION_NEUTRAL;
}

const char *rf_face_emotion_name(rf_emotion_t emotion) {
    switch (emotion) {
    case RF_EMOTION_HAPPY:
        return "happy";
    case RF_EMOTION_SAD:
        return "sad";
    case RF_EMOTION_SURPRISED:
        return "surprised";
    case RF_EMOTION_WORRIED:
        return "worried";
    case RF_EMOTION_SLEEPY:
        return "sleepy";
    case RF_EMOTION_THINKING:
        return "thinking";
    case RF_EMOTION_ANGRY:
        return "angry";
    case RF_EMOTION_NEUTRAL:
    case RF_EMOTION_COUNT:
    default:
        return "neutral";
    }
}

int rf_face_format(const rf_face_t *face, char *dst, size_t dst_len) {
    if (!face || !dst || dst_len == 0) {
        return -1;
    }
    const char *eyes = "o  o";
    if (face->blinking || face->eye_open < 20) {
        eyes = "-  -";
    } else if (face->emotion == RF_EMOTION_SURPRISED) {
        eyes = "O  O";
    } else if (face->emotion == RF_EMOTION_SLEEPY || face->eye_open < 60) {
        eyes = "-  o";
    } else if (face->emotion == RF_EMOTION_ANGRY) {
        eyes = ">  <";
    }
    const char *mouth = " __ ";
    switch (face->emotion) {
    case RF_EMOTION_HAPPY:
        mouth = " \\__/";
        break;
    case RF_EMOTION_SAD:
    case RF_EMOTION_WORRIED:
        mouth = " /\\  ";
        break;
    case RF_EMOTION_SURPRISED:
        mouth = " () ";
        break;
    case RF_EMOTION_THINKING:
        mouth = " ...";
        break;
    case RF_EMOTION_ANGRY:
        mouth = " == ";
        break;
    case RF_EMOTION_SLEEPY:
    case RF_EMOTION_NEUTRAL:
    case RF_EMOTION_COUNT:
    default:
        mouth = " __ ";
        break;
    }
    int n = snprintf(dst, dst_len,
                     "fox %s eye=%u%s\n"
                     "  /\\    /\\\n"
                     " (  %s  )\n"
                     "  \\ %s /\n"
                     "   ----\n",
                     rf_face_emotion_name(face->emotion), (unsigned)face->eye_open,
                     face->blinking ? " blink" : "", eyes, mouth);
    if (n < 0 || (size_t)n >= dst_len) {
        dst[dst_len - 1] = '\0';
        return -1;
    }
    return 0;
}

static void put_px(uint16_t *dst, int w, int h, int x, int y, uint16_t color) {
    if (x < 0 || y < 0 || x >= w || y >= h) {
        return;
    }
    dst[y * w + x] = color;
}

static int in_ellipse(int x, int y, int cx, int cy, int rx, int ry) {
    if (rx <= 0 || ry <= 0) {
        return 0;
    }
    int dx = x - cx;
    int dy = y - cy;
    long long rx2 = (long long)rx * rx;
    long long ry2 = (long long)ry * ry;
    return (long long)dx * dx * ry2 + (long long)dy * dy * rx2 <= rx2 * ry2;
}

static int in_tri(int x, int y, int x0, int y0, int x1, int y1, int x2, int y2) {
    int d1 = (x - x1) * (y0 - y1) - (x0 - x1) * (y - y1);
    int d2 = (x - x2) * (y1 - y2) - (x1 - x2) * (y - y2);
    int d3 = (x - x0) * (y2 - y0) - (x2 - x0) * (y - y0);
    int has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    int has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(has_neg && has_pos);
}

static int S(int v, int span) {
    return v * span / 100;
}

int rf_face_render_rgb565(const rf_face_t *face, uint16_t *dst, int width, int height) {
    if (!face || !dst || width <= 0 || height <= 0) {
        return -1;
    }
    const int w = width;
    const int h = height;
    for (int i = 0; i < w * h; i++) {
        dst[i] = RF_FACE_COLOR_BG;
    }

    const int lex0 = S(18, w), ley0 = S(40, h);
    const int lex1 = S(34, w), ley1 = S(8, h);
    const int lex2 = S(48, w), ley2 = S(42, h);
    const int rex0 = S(52, w), rey0 = S(42, h);
    const int rex1 = S(66, w), rey1 = S(8, h);
    const int rex2 = S(82, w), rey2 = S(40, h);
    const int cx = S(50, w);
    const int cy = S(58, h);
    const int rad = S(34, h);
    if (rad < 2) {
        return 0;
    }

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (in_tri(x, y, lex0, ley0, lex1, ley1, lex2, ley2) ||
                in_tri(x, y, rex0, rey0, rex1, rey1, rex2, rey2)) {
                put_px(dst, w, h, x, y, RF_FACE_COLOR_EAR);
            }
        }
    }
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (in_tri(x, y, S(26, w), S(36, h), S(34, w), S(16, h), S(42, w), S(36, h)) ||
                in_tri(x, y, S(58, w), S(36, h), S(66, w), S(16, h), S(74, w), S(36, h))) {
                put_px(dst, w, h, x, y, RF_FACE_COLOR_INNER_EAR);
            }
            if (in_ellipse(x, y, cx, cy, S(32, w), rad)) {
                put_px(dst, w, h, x, y, RF_FACE_COLOR_FUR);
            }
        }
    }
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (in_ellipse(x, y, cx, S(72, h), S(14, w), S(10, h))) {
                put_px(dst, w, h, x, y, RF_FACE_COLOR_SNOUT);
            }
        }
    }

    int open = face->eye_open;
    int ery = (S(11, h) * open) / 100;
    int erx = face->emotion == RF_EMOTION_SURPRISED ? S(9, w) : S(7, w);
    const int eye_y = S(54, h);
    const int left_x = S(38, w);
    const int right_x = S(62, w);
    if (ery < 1) {
        for (int dx = -erx; dx <= erx; dx++) {
            put_px(dst, w, h, left_x + dx, eye_y, RF_FACE_COLOR_PUPIL);
            put_px(dst, w, h, right_x + dx, eye_y, RF_FACE_COLOR_PUPIL);
        }
    } else {
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                if (in_ellipse(x, y, left_x, eye_y, erx, ery) ||
                    in_ellipse(x, y, right_x, eye_y, erx, ery)) {
                    put_px(dst, w, h, x, y, RF_FACE_COLOR_SCLERA);
                }
            }
        }
        int pr = erx / 2;
        if (pr < 1) {
            pr = 1;
        }
        int pry = ery / 2;
        if (pry < 1) {
            pry = 1;
        }
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                if (in_ellipse(x, y, left_x, eye_y, pr, pry) ||
                    in_ellipse(x, y, right_x, eye_y, pr, pry)) {
                    put_px(dst, w, h, x, y, RF_FACE_COLOR_PUPIL);
                }
            }
        }
    }

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (in_tri(x, y, S(46, w), S(66, h), S(50, w), S(74, h), S(54, w), S(66, h))) {
                put_px(dst, w, h, x, y, RF_FACE_COLOR_NOSE);
            }
        }
    }

    int mouth_cx = cx;
    int mouth_cy = S(80, h);
    int half = S(12, w);
    if (face->emotion == RF_EMOTION_SURPRISED) {
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                if (in_ellipse(x, y, mouth_cx, mouth_cy, S(5, w), S(6, h))) {
                    put_px(dst, w, h, x, y, RF_FACE_COLOR_MOUTH);
                }
            }
        }
    } else {
        for (int dx = -half; dx <= half; dx++) {
            int curve = (dx * dx) / (S(8, w) > 0 ? S(8, w) : 1);
            int y = mouth_cy;
            if (face->emotion == RF_EMOTION_HAPPY) {
                y = mouth_cy + curve / 4;
            } else if (face->emotion == RF_EMOTION_SAD || face->emotion == RF_EMOTION_WORRIED) {
                y = mouth_cy - curve / 4;
            } else if (face->emotion == RF_EMOTION_THINKING) {
                if (dx < -half / 3 || dx > half / 3) {
                    continue;
                }
            }
            put_px(dst, w, h, mouth_cx + dx, y, RF_FACE_COLOR_MOUTH);
            put_px(dst, w, h, mouth_cx + dx, y + 1, RF_FACE_COLOR_MOUTH);
        }
    }
    return 0;
}
