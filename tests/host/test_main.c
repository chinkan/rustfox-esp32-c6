// SPDX-License-Identifier: MIT
// Copyright (c) 2026 chinkan.ai

#include "rf_face.h"
#include "rf_frames.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failed;

#define EXPECT(cond)                                                                             \
    do {                                                                                         \
        if (!(cond)) {                                                                           \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                      \
            g_failed++;                                                                          \
        }                                                                                        \
    } while (0)

static int count_color(const uint16_t *px, size_t n, uint16_t color) {
    int c = 0;
    for (size_t i = 0; i < n; i++) {
        if (px[i] == color) {
            c++;
        }
    }
    return c;
}

static uint32_t checksum(const uint16_t *px, size_t n) {
    uint32_t s = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        s ^= px[i];
        s *= 16777619u;
    }
    return s;
}

static void test_frames(void) {
    char buf[512];
    rf_frame_t frame;

    int n = rf_encode_ping(buf, sizeof(buf));
    EXPECT(n == (int)strlen("{\"type\":\"ping\"}"));
    EXPECT(strcmp(buf, "{\"type\":\"ping\"}") == 0);
    EXPECT(rf_parse_frame(buf, (size_t)n, &frame) == RF_OK);
    EXPECT(frame.type == RF_TYPE_PONG || frame.type == RF_TYPE_PING);
    EXPECT(frame.type == RF_TYPE_PING);

    n = rf_encode_auth(buf, sizeof(buf), "a\"b");
    EXPECT(strcmp(buf, "{\"type\":\"auth\",\"token\":\"a\\\"b\"}") == 0);
    EXPECT(rf_parse_frame(buf, (size_t)n, &frame) == RF_OK);
    EXPECT(frame.type == RF_TYPE_AUTH);
    EXPECT(strcmp(frame.token, "a\"b") == 0);

    EXPECT(rf_encode_auth(buf, sizeof(buf), "   ") == RF_ERR_ARG);
    EXPECT(rf_encode_user_text(buf, sizeof(buf), "   ") == RF_ERR_BAD_FRAME);

    n = rf_encode_user_text(buf, sizeof(buf), "  turn on the \"lamp\"  ");
    EXPECT(n > 0);
    EXPECT(strcmp(buf, "{\"type\":\"user_text\",\"text\":\"turn on the \\\"lamp\\\"\"}") == 0);
    EXPECT(rf_parse_frame(buf, (size_t)n, &frame) == RF_OK);
    EXPECT(frame.type == RF_TYPE_USER_TEXT);
    EXPECT(strcmp(frame.text, "turn on the \"lamp\"") == 0);

    n = rf_encode_user_text(buf, sizeof(buf), "燈");
    EXPECT(n > 0);
    EXPECT(rf_parse_frame(buf, (size_t)n, &frame) == RF_OK);
    EXPECT(strcmp(frame.text, "燈") == 0);

    n = rf_encode_device_hello(buf, sizeof(buf), "ab[cd]", " kitchen ", "0.1.0");
    EXPECT(n > 0);
    EXPECT(strcmp(buf, "{\"type\":\"device_hello\",\"device_id\":\"abcd\",\"room\":\"kitchen\","
                       "\"fw_version\":\"0.1.0\"}") == 0);
    EXPECT(rf_parse_frame(buf, (size_t)n, &frame) == RF_OK);
    EXPECT(frame.type == RF_TYPE_DEVICE_HELLO);
    EXPECT(strcmp(frame.device_id, "abcd") == 0);
    EXPECT(strcmp(frame.room, "kitchen") == 0);
    EXPECT(strcmp(frame.fw_version, "0.1.0") == 0);

    n = rf_encode_device_hello(buf, sizeof(buf), "board-1", NULL, NULL);
    EXPECT(strcmp(buf, "{\"type\":\"device_hello\",\"device_id\":\"board-1\"}") == 0);

    char id[80];
    memset(id, 'x', sizeof(id) - 1);
    id[sizeof(id) - 1] = '\0';
    n = rf_encode_device_hello(buf, sizeof(buf), id, "", "");
    EXPECT(n > 0);
    EXPECT(rf_parse_frame(buf, (size_t)n, &frame) == RF_OK);
    EXPECT(strlen(frame.device_id) == RF_META_MAX_CHARS);

    const char *ack =
        "{\"type\":\"device_ack\",\"status\":\"authenticated\",\"device_id\":\"abc\",\"room\":\"kitchen\"}";
    EXPECT(rf_parse_frame(ack, strlen(ack), &frame) == RF_OK);
    EXPECT(frame.type == RF_TYPE_DEVICE_ACK);
    EXPECT(strcmp(frame.status, "authenticated") == 0);
    EXPECT(strcmp(frame.device_id, "abc") == 0);
    EXPECT(strcmp(frame.room, "kitchen") == 0);

    const char *hello_ack = "  {\"type\" : \"device_ack\", \"status\" : \"hello\", "
                            "\"device_id\" : \"board-1\"}\n";
    EXPECT(rf_parse_frame(hello_ack, strlen(hello_ack), &frame) == RF_OK);
    EXPECT(strcmp(frame.status, "hello") == 0);
    EXPECT(frame.room[0] == '\0');

    const char *asst = "{\"type\":\"assistant_text\",\"text\":\"say \\\"hi\\\"\"}";
    EXPECT(rf_parse_frame(asst, strlen(asst), &frame) == RF_OK);
    EXPECT(frame.type == RF_TYPE_ASSISTANT_TEXT);
    EXPECT(strcmp(frame.text, "say \"hi\"") == 0);

    const char *err = "{\"type\":\"error\",\"code\":\"unauthorized\",\"message\":\"invalid device token\"}";
    EXPECT(rf_parse_frame(err, strlen(err), &frame) == RF_OK);
    EXPECT(frame.type == RF_TYPE_ERROR);
    EXPECT(strcmp(frame.code, "unauthorized") == 0);
    EXPECT(strcmp(frame.message, "invalid device token") == 0);

    const char *pong = "{\"type\":\"pong\",\"ignored\":1}";
    EXPECT(rf_parse_frame(pong, strlen(pong), &frame) == RF_OK);
    EXPECT(frame.type == RF_TYPE_PONG);

    const char *uni = "{\"type\":\"assistant_text\",\"text\":\"\\u71c8\"}";
    EXPECT(rf_parse_frame(uni, strlen(uni), &frame) == RF_OK);
    EXPECT(strcmp(frame.text, "燈") == 0);

    EXPECT(rf_parse_frame("{", 1, &frame) == RF_ERR_BAD_FRAME);
    EXPECT(rf_parse_frame("[]", 2, &frame) == RF_ERR_BAD_FRAME);
    EXPECT(rf_parse_frame("{\"type\":1}", strlen("{\"type\":1}"), &frame) == RF_ERR_BAD_FRAME);
    EXPECT(rf_parse_frame("{\"nope\":\"x\"}", strlen("{\"nope\":\"x\"}"), &frame) == RF_ERR_BAD_FRAME);
    EXPECT(rf_parse_frame("{\"type\":\"pong\"} trailing", strlen("{\"type\":\"pong\"} trailing"),
                          &frame) == RF_ERR_BAD_FRAME);

    const char *unknown = "{\"type\":\"future\"}";
    EXPECT(rf_parse_frame(unknown, strlen(unknown), &frame) == RF_OK);
    EXPECT(frame.type == RF_TYPE_UNKNOWN);

    char *big = malloc(RF_MAX_USER_TEXT_CHARS + 1);
    char *dst = malloc(RF_MAX_USER_TEXT_CHARS + 64);
    EXPECT(big && dst);
    if (big && dst) {
        memset(big, 'a', RF_MAX_USER_TEXT_CHARS);
        big[RF_MAX_USER_TEXT_CHARS] = '\0';
        int fit = rf_encode_user_text(dst, RF_MAX_USER_TEXT_CHARS + 64, big);
        EXPECT(fit > RF_MAX_USER_TEXT_CHARS);
        big[RF_MAX_USER_TEXT_CHARS - 1] = '\0';
        memset(big, 'b', RF_MAX_USER_TEXT_CHARS);
        big[RF_MAX_USER_TEXT_CHARS] = '\0';
        /* 16001 code points */
        char *too = malloc(RF_MAX_USER_TEXT_CHARS + 2);
        EXPECT(too != NULL);
        if (too) {
            memset(too, 'c', RF_MAX_USER_TEXT_CHARS + 1);
            too[RF_MAX_USER_TEXT_CHARS + 1] = '\0';
            EXPECT(rf_encode_user_text(dst, RF_MAX_USER_TEXT_CHARS + 64, too) == RF_ERR_RANGE);
            free(too);
        }
        EXPECT(fit > 0);
    }
    free(big);
    free(dst);

    EXPECT(strcmp(rf_frame_type_name(RF_TYPE_USER_TEXT), "user_text") == 0);
    EXPECT(strcmp(RF_WS_PATH, "/api/device/v1/ws") == 0);
    EXPECT(RF_MAX_USER_TEXT_CHARS == 16000);
    EXPECT(RF_AUTH_WAIT_MS == 15000);
}

static void test_face(void) {
    rf_face_t face;
    rf_face_init(&face);
    EXPECT(face.emotion == RF_EMOTION_SLEEPY);
    EXPECT(face.eye_open == 35);

    rf_face_set_emotion(&face, RF_EMOTION_NEUTRAL);
    EXPECT(face.eye_open == 100);
    EXPECT(rf_face_tick(&face, 1) == 0 || face.eye_open == 100);

    face.blink_interval_ms = 1000;
    face.blink_hold_ms = 100;
    face.ms_since_blink = 0;
    face.blinking = 0;
    EXPECT(rf_face_tick(&face, 1000) == 1);
    EXPECT(face.blinking == 1);
    EXPECT(face.eye_open == 0);
    EXPECT(rf_face_tick(&face, 100) == 1);
    EXPECT(face.blinking == 0);
    EXPECT(face.eye_open == 100);

    EXPECT(rf_face_emotion_from_text("Great job") == RF_EMOTION_HAPPY);
    EXPECT(rf_face_emotion_from_text("I cannot do that") == RF_EMOTION_SAD);
    EXPECT(rf_face_emotion_from_text("好的") == RF_EMOTION_HAPPY);
    EXPECT(rf_face_emotion_from_text("抱歉") == RF_EMOTION_SAD);
    EXPECT(rf_face_emotion_from_text("hello there") == RF_EMOTION_NEUTRAL);
    EXPECT(strcmp(rf_face_emotion_name(RF_EMOTION_WORRIED), "worried") == 0);

    char art[192];
    EXPECT(rf_face_format(&face, art, sizeof(art)) == 0);
    EXPECT(strstr(art, "neutral") != NULL);
    EXPECT(strstr(art, "/\\") != NULL);

    static uint16_t open_px[RF_FACE_W * RF_FACE_H];
    static uint16_t shut_px[RF_FACE_W * RF_FACE_H];
    static uint16_t happy_px[RF_FACE_W * RF_FACE_H];
    static uint16_t sad_px[RF_FACE_W * RF_FACE_H];
    face.blinking = 0;
    face.eye_open = 100;
    face.emotion = RF_EMOTION_NEUTRAL;
    EXPECT(rf_face_render_rgb565(&face, open_px, RF_FACE_W, RF_FACE_H) == 0);
    face.eye_open = 0;
    face.blinking = 1;
    EXPECT(rf_face_render_rgb565(&face, shut_px, RF_FACE_W, RF_FACE_H) == 0);
    int open_white = count_color(open_px, RF_FACE_W * RF_FACE_H, RF_FACE_COLOR_SCLERA);
    int shut_white = count_color(shut_px, RF_FACE_W * RF_FACE_H, RF_FACE_COLOR_SCLERA);
    EXPECT(open_white > shut_white);
    EXPECT(shut_white == 0);

    face.blinking = 0;
    face.eye_open = 90;
    face.emotion = RF_EMOTION_HAPPY;
    rf_face_render_rgb565(&face, happy_px, RF_FACE_W, RF_FACE_H);
    face.emotion = RF_EMOTION_SAD;
    face.eye_open = 55;
    rf_face_render_rgb565(&face, sad_px, RF_FACE_W, RF_FACE_H);
    EXPECT(checksum(happy_px, RF_FACE_W * RF_FACE_H) != checksum(sad_px, RF_FACE_W * RF_FACE_H));
    EXPECT(count_color(happy_px, RF_FACE_W * RF_FACE_H, RF_FACE_COLOR_EAR) > 0);
    EXPECT(rf_face_render_rgb565(NULL, open_px, 8, 8) == -1);
}

int main(void) {
    test_frames();
    test_face();
    if (g_failed) {
        fprintf(stderr, "%d assertion(s) failed\n", g_failed);
        return 1;
    }
    printf("host tests ok\n");
    return 0;
}
