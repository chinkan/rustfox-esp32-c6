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

typedef struct {
    int eye;
    int look;
    int smile;
    int mouth;
    int worry;
    int perk;
} face_target_t;

static void targets_for(rf_emotion_t emotion, rf_cue_t cue, face_target_t *t) {
    memset(t, 0, sizeof(*t));
    t->eye = 1000;
    switch (emotion) {
    case RF_EMOTION_HAPPY:
        t->eye = 850;
        t->smile = 900;
        t->mouth = 180;
        break;
    case RF_EMOTION_SAD:
        t->eye = 550;
        t->smile = -800;
        break;
    case RF_EMOTION_SURPRISED:
        t->eye = 1000;
        t->mouth = 750;
        break;
    case RF_EMOTION_WORRIED:
        t->eye = 800;
        t->smile = -500;
        t->worry = 1000;
        break;
    case RF_EMOTION_SLEEPY:
        t->eye = 350;
        break;
    case RF_EMOTION_THINKING:
        t->eye = 700;
        t->look = 650;
        t->mouth = 80;
        break;
    case RF_EMOTION_ANGRY:
        t->eye = 500;
        t->smile = -400;
        t->worry = 400;
        break;
    case RF_EMOTION_NEUTRAL:
    case RF_EMOTION_COUNT:
    default:
        break;
    }
    if (emotion == RF_EMOTION_WORRIED) {
        return;
    }
    if (cue == RF_CUE_LISTENING || cue == RF_CUE_HOLD_CAP) {
        t->perk = 1000;
        t->eye = 1000;
        t->mouth = 220;
        t->smile = 150;
        t->look = 0;
    } else if (cue == RF_CUE_SPEAKING) {
        t->perk = 400;
        t->smile = 300;
        t->mouth = 600;
        t->look = 0;
        if (t->eye < 900) {
            t->eye = 900;
        }
    }
}

static void apply_targets(rf_face_t *face, int snap) {
    face_target_t t;
    targets_for(face->emotion, face->cue, &t);
    if (!snap && t.eye == face->eye_to && t.look == face->look_to && t.smile == face->smile_to &&
        t.mouth == face->mouth_to && t.worry == face->worry_to && t.perk == face->perk_to) {
        return;
    }
    face->eye_from = snap ? (int16_t)t.eye : face->eye_q;
    face->look_from = snap ? (int16_t)t.look : face->look_q;
    face->smile_from = snap ? (int16_t)t.smile : face->smile_q;
    face->mouth_from = snap ? (int16_t)t.mouth : face->mouth_q;
    face->worry_from = snap ? (int16_t)t.worry : face->worry_q;
    face->perk_from = snap ? (int16_t)t.perk : face->perk_q;
    face->eye_to = (int16_t)t.eye;
    face->look_to = (int16_t)t.look;
    face->smile_to = (int16_t)t.smile;
    face->mouth_to = (int16_t)t.mouth;
    face->worry_to = (int16_t)t.worry;
    face->perk_to = (int16_t)t.perk;
    if (snap) {
        face->eye_q = face->eye_to;
        face->look_q = face->look_to;
        face->smile_q = face->smile_to;
        face->mouth_q = face->mouth_to;
        face->worry_q = face->worry_to;
        face->perk_q = face->perk_to;
        face->lerp_ms = RF_FACE_LERP_MS;
        face->eye_open = (uint8_t)(face->eye_q / 10);
    } else {
        face->lerp_ms = 0;
    }
}

void rf_face_init(rf_face_t *face) {
    if (!face) {
        return;
    }
    memset(face, 0, sizeof(*face));
    face->emotion = RF_EMOTION_SLEEPY;
    face->cue = RF_CUE_NONE;
    face->blink_interval_ms = 3200;
    face->blink_hold_ms = 140;
    apply_targets(face, 1);
}

void rf_face_set_emotion(rf_face_t *face, rf_emotion_t emotion) {
    if (!face || emotion >= RF_EMOTION_COUNT) {
        return;
    }
    face->emotion = emotion;
    apply_targets(face, 0);
}

void rf_face_set_cue(rf_face_t *face, rf_cue_t cue) {
    if (!face || cue >= RF_CUE_COUNT) {
        return;
    }
    face->cue = cue;
    apply_targets(face, 0);
}

static int smoothstep_u(uint32_t t, uint32_t d) {
    if (d == 0 || t >= d) {
        return 1000;
    }
    uint32_t x = (uint32_t)(((uint64_t)t * 1000) / d);
    uint32_t x2 = (x * x) / 1000;
    uint32_t s = (x2 * (3000 - (2 * x))) / 1000;
    if (s > 1000) {
        s = 1000;
    }
    return (int)s;
}

static int lerp_ss(int from, int to, uint32_t t, uint32_t d) {
    int u = smoothstep_u(t, d);
    return from + (int)(((int64_t)(to - from) * u) / 1000);
}

static int isin_1024(uint32_t phase) {
    static const int16_t k_sin[17] = {0,   98,  195, 290, 383, 471, 556, 634, 707,
                                      773, 831, 882, 924, 957, 981, 995, 1000};
    uint32_t p = phase & 1023u;
    uint32_t quad = p >> 8;
    uint32_t q = p & 255u;
    int idx = (int)((q * 16) / 256);
    int v = k_sin[idx];
    if (quad == 1 || quad == 2) {
        v = k_sin[16 - idx];
    }
    if (quad >= 2) {
        v = -v;
    }
    return v;
}

static int isin_period(uint32_t ms, uint32_t period) {
    if (period == 0) {
        return 0;
    }
    uint32_t phase = (uint32_t)(((uint64_t)(ms % period) * 1024) / period);
    return isin_1024(phase);
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
    uint8_t prev_blink = face->blinking;
    int16_t prev_smile = face->smile_q;
    int16_t prev_mouth = face->mouth_q;
    int16_t prev_worry = face->worry_q;
    int16_t prev_perk = face->perk_q;
    int16_t prev_look = face->look_q;
    int16_t prev_sway = face->sway_bucket;
    rf_emotion_t prev = face->emotion;

    face->anim_ms += elapsed_ms;
    if (face->lerp_ms < RF_FACE_LERP_MS) {
        uint32_t room = RF_FACE_LERP_MS - face->lerp_ms;
        face->lerp_ms += elapsed_ms < room ? elapsed_ms : room;
    }
    face->eye_q = (int16_t)lerp_ss(face->eye_from, face->eye_to, face->lerp_ms, RF_FACE_LERP_MS);
    face->look_q = (int16_t)lerp_ss(face->look_from, face->look_to, face->lerp_ms, RF_FACE_LERP_MS);
    face->smile_q = (int16_t)lerp_ss(face->smile_from, face->smile_to, face->lerp_ms, RF_FACE_LERP_MS);
    face->mouth_q = (int16_t)lerp_ss(face->mouth_from, face->mouth_to, face->lerp_ms, RF_FACE_LERP_MS);
    face->worry_q = (int16_t)lerp_ss(face->worry_from, face->worry_to, face->lerp_ms, RF_FACE_LERP_MS);
    face->perk_q = (int16_t)lerp_ss(face->perk_from, face->perk_to, face->lerp_ms, RF_FACE_LERP_MS);

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

    int factor = 1000;
    if (face->blinking) {
        uint32_t half = face->blink_hold_ms / 2;
        if (half == 0) {
            half = 1;
        }
        if (face->blink_elapsed < half) {
            factor = 1000 - smoothstep_u(face->blink_elapsed, half);
        } else {
            uint32_t into = face->blink_elapsed - half;
            uint32_t open_d = face->blink_hold_ms - half;
            if (open_d == 0) {
                open_d = 1;
            }
            factor = smoothstep_u(into, open_d);
        }
    }
    int shown = ((int)face->eye_q * factor) / 1000;
    if (shown < 0) {
        shown = 0;
    }
    if (shown > 1000) {
        shown = 1000;
    }
    face->eye_open = (uint8_t)(shown / 10);
    face->sway_bucket = (int16_t)(isin_period(face->anim_ms, 2800) / 64);

    return face->eye_open != prev_eye || face->blinking != prev_blink || face->emotion != prev ||
           face->smile_q != prev_smile || face->mouth_q != prev_mouth || face->worry_q != prev_worry ||
           face->perk_q != prev_perk || face->look_q != prev_look || face->sway_bucket != prev_sway;
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
    const char *cue = "";
    if (face->cue == RF_CUE_LISTENING) {
        cue = " listen";
    } else if (face->cue == RF_CUE_SPEAKING) {
        cue = " speak";
    } else if (face->cue == RF_CUE_HOLD_CAP) {
        cue = " hold-cap";
    }
    int n = snprintf(dst, dst_len,
                     "fox %s eye=%u%s%s\n"
                     "  /\\    /\\\n"
                     " (  %s  )\n"
                     "  \\ %s /\n"
                     "   ----\n",
                     rf_face_emotion_name(face->emotion), (unsigned)face->eye_open,
                     face->blinking ? " blink" : "", cue, eyes, mouth);
    if (n < 0 || (size_t)n >= dst_len) {
        dst[dst_len - 1] = '\0';
        return -1;
    }
    return 0;
}

static int u_to_px(int u, int span) {
    return (int)(((int64_t)u * span) / 1000);
}

static void put_px(uint16_t *dst, int width, int y0, int rows, int x, int y, uint16_t color) {
    if (x < 0 || x >= width || y < y0 || y >= y0 + rows) {
        return;
    }
    dst[(y - y0) * width + x] = color;
}

static int in_ellipse(int x, int y, int cx, int cy, int rx, int ry) {
    if (rx <= 0 || ry <= 0) {
        return 0;
    }
    int64_t dx = (int64_t)x - cx;
    int64_t dy = (int64_t)y - cy;
    int64_t rx2 = (int64_t)rx * rx;
    int64_t ry2 = (int64_t)ry * ry;
    return dx * dx * ry2 + dy * dy * rx2 <= rx2 * ry2;
}

static int in_tri(int x, int y, int x0, int y0, int x1, int y1, int x2, int y2) {
    int64_t d1 = (int64_t)(x - x1) * (y0 - y1) - (int64_t)(x0 - x1) * (y - y1);
    int64_t d2 = (int64_t)(x - x2) * (y1 - y2) - (int64_t)(x1 - x2) * (y - y2);
    int64_t d3 = (int64_t)(x - x0) * (y2 - y0) - (int64_t)(x2 - x0) * (y - y0);
    int has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    int has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(has_neg && has_pos);
}

static void fill_tri(uint16_t *dst, int width, int y0, int rows, int x0, int y_a, int x1, int y_b,
                     int x2, int y_c, uint16_t color) {
    int minx = x0 < x1 ? x0 : x1;
    if (x2 < minx) {
        minx = x2;
    }
    int maxx = x0 > x1 ? x0 : x1;
    if (x2 > maxx) {
        maxx = x2;
    }
    int miny = y_a < y_b ? y_a : y_b;
    if (y_c < miny) {
        miny = y_c;
    }
    int maxy = y_a > y_b ? y_a : y_b;
    if (y_c > maxy) {
        maxy = y_c;
    }
    if (minx < 0) {
        minx = 0;
    }
    if (maxx >= width) {
        maxx = width - 1;
    }
    if (miny < y0) {
        miny = y0;
    }
    if (maxy >= y0 + rows) {
        maxy = y0 + rows - 1;
    }
    for (int y = miny; y <= maxy; y++) {
        for (int x = minx; x <= maxx; x++) {
            if (in_tri(x, y, x0, y_a, x1, y_b, x2, y_c)) {
                put_px(dst, width, y0, rows, x, y, color);
            }
        }
    }
}

static void fill_ellipse(uint16_t *dst, int width, int y0, int rows, int cx, int cy, int rx, int ry,
                         uint16_t color) {
    if (rx <= 0 || ry <= 0) {
        return;
    }
    int minx = cx - rx;
    int maxx = cx + rx;
    int miny = cy - ry;
    int maxy = cy + ry;
    if (minx < 0) {
        minx = 0;
    }
    if (maxx >= width) {
        maxx = width - 1;
    }
    if (miny < y0) {
        miny = y0;
    }
    if (maxy >= y0 + rows) {
        maxy = y0 + rows - 1;
    }
    for (int y = miny; y <= maxy; y++) {
        for (int x = minx; x <= maxx; x++) {
            if (in_ellipse(x, y, cx, cy, rx, ry)) {
                put_px(dst, width, y0, rows, x, y, color);
            }
        }
    }
}

static void hline(uint16_t *dst, int width, int y0, int rows, int x, int y, int half, uint16_t color) {
    for (int dx = -half; dx <= half; dx++) {
        put_px(dst, width, y0, rows, x + dx, y, color);
        put_px(dst, width, y0, rows, x + dx, y + 1, color);
    }
}

int rf_face_render_stripe(const rf_face_t *face, uint16_t *dst, int width, int y0, int rows,
                          int panel_h) {
    if (!face || !dst || width <= 0 || rows <= 0 || panel_h <= 0 || y0 < 0 || y0 >= panel_h) {
        return -1;
    }
    if (y0 + rows > panel_h) {
        return -1;
    }
    const int w = width;
    const int h = panel_h;
    for (int i = 0; i < w * rows; i++) {
        dst[i] = RF_FACE_COLOR_BG;
    }

    int breathe = isin_period(face->anim_ms, 3200) / 25;
    int scale = 1000 + breathe;
    int sway = isin_period(face->anim_ms, 2800);
    sway = (int)(((int64_t)sway * (1000 - face->perk_q)) / 1000);
    int tip_dx = (sway * 70) / 1000;
    int perk_lift = (face->perk_q * 40) / 1000;

    int lex0 = u_to_px(20, w), ley0 = u_to_px(480, h);
    int lex1 = u_to_px(70 + tip_dx, w), ley1 = u_to_px(-120 - perk_lift, h);
    int lex2 = u_to_px(480, w), ley2 = u_to_px(450, h);
    int rex0 = u_to_px(520, w), rey0 = u_to_px(450, h);
    int rex1 = u_to_px(930 - tip_dx, w), rey1 = u_to_px(-120 - perk_lift, h);
    int rex2 = u_to_px(980, w), rey2 = u_to_px(480, h);
    fill_tri(dst, w, y0, rows, lex0, ley0, lex1, ley1, lex2, ley2, RF_FACE_COLOR_EAR);
    fill_tri(dst, w, y0, rows, rex0, rey0, rex1, rey1, rex2, rey2, RF_FACE_COLOR_EAR);
    fill_tri(dst, w, y0, rows, u_to_px(90, w), u_to_px(420, h), u_to_px(140 + tip_dx / 2, w),
             u_to_px(-20 - perk_lift / 2, h), u_to_px(360, w), u_to_px(400, h), RF_FACE_COLOR_INNER_EAR);
    fill_tri(dst, w, y0, rows, u_to_px(640, w), u_to_px(400, h), u_to_px(860 - tip_dx / 2, w),
             u_to_px(-20 - perk_lift / 2, h), u_to_px(910, w), u_to_px(420, h), RF_FACE_COLOR_INNER_EAR);

    int cx = u_to_px(500, w);
    int cy = u_to_px(560, h);
    int rx = u_to_px(470, w) * scale / 1000;
    int ry = u_to_px(450, h) * scale / 1000;
    fill_ellipse(dst, w, y0, rows, cx, cy, rx, ry, RF_FACE_COLOR_FUR);
    fill_ellipse(dst, w, y0, rows, cx, u_to_px(740, h), u_to_px(220, w), u_to_px(160, h),
                 RF_FACE_COLOR_SNOUT);

    int open = face->eye_open;
    int ery = (u_to_px(130, h) * open) / 100;
    int erx = face->emotion == RF_EMOTION_SURPRISED ? u_to_px(110, w) : u_to_px(90, w);
    int look = (face->look_q * u_to_px(36, w)) / 1000;
    int eye_y = u_to_px(500, h);
    int left_x = u_to_px(340, w) + look;
    int right_x = u_to_px(660, w) + look;
    if (ery < 2) {
        hline(dst, w, y0, rows, left_x, eye_y, erx, RF_FACE_COLOR_PUPIL);
        hline(dst, w, y0, rows, right_x, eye_y, erx, RF_FACE_COLOR_PUPIL);
    } else {
        fill_ellipse(dst, w, y0, rows, left_x, eye_y, erx, ery, RF_FACE_COLOR_SCLERA);
        fill_ellipse(dst, w, y0, rows, right_x, eye_y, erx, ery, RF_FACE_COLOR_SCLERA);
        int pr = erx / 2;
        int pry = ery / 2;
        if (pr < 1) {
            pr = 1;
        }
        if (pry < 1) {
            pry = 1;
        }
        fill_ellipse(dst, w, y0, rows, left_x, eye_y, pr, pry, RF_FACE_COLOR_PUPIL);
        fill_ellipse(dst, w, y0, rows, right_x, eye_y, pr, pry, RF_FACE_COLOR_PUPIL);
    }

    fill_tri(dst, w, y0, rows, u_to_px(440, w), u_to_px(640, h), u_to_px(500, w), u_to_px(760, h),
             u_to_px(560, w), u_to_px(640, h), RF_FACE_COLOR_NOSE);

    int mouth = face->mouth_q;
    if (face->cue == RF_CUE_SPEAKING && face->emotion != RF_EMOTION_WORRIED) {
        int wobble = (isin_period(face->anim_ms, 280) + 1000) / 2;
        mouth = 200 + (wobble * 700) / 1000;
    }
    if (mouth < 0) {
        mouth = 0;
    }
    if (mouth > 1000) {
        mouth = 1000;
    }
    int mouth_cx = cx;
    int mouth_cy = u_to_px(830, h);
    int half = u_to_px(200, w);
    int smile = face->smile_q;
    if (mouth > 160) {
        int mry = u_to_px(36, h) + (u_to_px(110, h) * mouth) / 1000;
        int mrx = half;
        int drop = (smile * u_to_px(36, h)) / 1000;
        fill_ellipse(dst, w, y0, rows, mouth_cx, mouth_cy + drop, mrx, mry, RF_FACE_COLOR_MOUTH);
    } else if (half > 0) {
        int denom = u_to_px(90, w);
        if (denom < 1) {
            denom = 1;
        }
        for (int dx = -half; dx <= half; dx++) {
            int curve = (dx * dx) / denom;
            int bend = (curve * (smile >= 0 ? smile : -smile)) / 1000;
            if (bend > u_to_px(80, h)) {
                bend = u_to_px(80, h);
            }
            int y = mouth_cy + (smile >= 0 ? bend : -bend);
            put_px(dst, w, y0, rows, mouth_cx + dx, y, RF_FACE_COLOR_MOUTH);
            put_px(dst, w, y0, rows, mouth_cx + dx, y + 1, RF_FACE_COLOR_MOUTH);
            put_px(dst, w, y0, rows, mouth_cx + dx, y + 2, RF_FACE_COLOR_MOUTH);
        }
    }

    if (face->worry_q > 600) {
        int m = u_to_px(18, w);
        int len = u_to_px(70, w);
        int thick = u_to_px(8, w);
        if (thick < 2) {
            thick = 2;
        }
        for (int i = 0; i < len; i++) {
            for (int t = 0; t < thick; t++) {
                put_px(dst, w, y0, rows, m + i, m + t, RF_FACE_COLOR_ALERT);
                put_px(dst, w, y0, rows, m + t, m + i, RF_FACE_COLOR_ALERT);
                put_px(dst, w, y0, rows, w - 1 - m - i, m + t, RF_FACE_COLOR_ALERT);
                put_px(dst, w, y0, rows, w - 1 - m - t, m + i, RF_FACE_COLOR_ALERT);
                put_px(dst, w, y0, rows, m + i, h - 1 - m - t, RF_FACE_COLOR_ALERT);
                put_px(dst, w, y0, rows, m + t, h - 1 - m - i, RF_FACE_COLOR_ALERT);
                put_px(dst, w, y0, rows, w - 1 - m - i, h - 1 - m - t, RF_FACE_COLOR_ALERT);
                put_px(dst, w, y0, rows, w - 1 - m - t, h - 1 - m - i, RF_FACE_COLOR_ALERT);
            }
        }
    }

    if (face->cue == RF_CUE_HOLD_CAP && face->emotion != RF_EMOTION_WORRIED) {
        int rad = (w < h ? w : h) / 2 - 3;
        int band = w / 80;
        if (band < 2) {
            band = 2;
        }
        int r0 = rad - band;
        if (r0 < 1) {
            r0 = 1;
        }
        int miny = y0;
        int maxy = y0 + rows - 1;
        int pcx = w / 2;
        int pcy = h / 2;
        for (int y = miny; y <= maxy; y++) {
            for (int x = 0; x < w; x++) {
                int64_t dx = (int64_t)x - pcx;
                int64_t dy = (int64_t)y - pcy;
                int64_t d2 = dx * dx + dy * dy;
                if (d2 <= (int64_t)rad * rad && d2 >= (int64_t)r0 * r0) {
                    dst[(y - y0) * w + x] = RF_FACE_COLOR_CUE;
                }
            }
        }
    }
    return 0;
}

int rf_face_render_rgb565(const rf_face_t *face, uint16_t *dst, int width, int height) {
    if (!face || !dst || width <= 0 || height <= 0) {
        return -1;
    }
    for (int y = 0; y < height;) {
        int rows = 16;
        if (y + rows > height) {
            rows = height - y;
        }
        if (rf_face_render_stripe(face, dst + (size_t)y * (size_t)width, width, y, rows, height) != 0) {
            return -1;
        }
        y += rows;
    }
    return 0;
}
