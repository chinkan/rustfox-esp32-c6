// SPDX-License-Identifier: MIT
// Copyright (c) 2026 chinkan.ai

#include "rf_face.h"
#include "rf_frames.h"
#include "rf_talk.h"

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

    const char *audio =
        "{\"type\":\"assistant_audio\",\"id\":\"clip-1\",\"url\":\"/api/device/v1/audio/clip-1\","
        "\"sample_rate\":16000,\"channels\":1,\"encoding\":\"s16le\"}";
    EXPECT(rf_parse_frame(audio, strlen(audio), &frame) == RF_OK);
    EXPECT(frame.type == RF_TYPE_ASSISTANT_AUDIO);
    EXPECT(rf_frame_audio_playable(&frame) == 1);
    EXPECT(strcmp(frame.audio_id, "clip-1") == 0);

    const char *bad_rate =
        "{\"type\":\"assistant_audio\",\"id\":\"clip-1\",\"url\":\"/api/device/v1/audio/clip-1\","
        "\"sample_rate\":24000,\"channels\":1,\"encoding\":\"s16le\"}";
    EXPECT(rf_parse_frame(bad_rate, strlen(bad_rate), &frame) == RF_OK);
    EXPECT(rf_frame_audio_playable(&frame) == 0);

    const char *opus =
        "{\"type\":\"assistant_audio\",\"id\":\"clip-1\",\"url\":\"/api/device/v1/audio/clip-1\","
        "\"sample_rate\":16000,\"channels\":1,\"encoding\":\"opus\"}";
    EXPECT(rf_parse_frame(opus, strlen(opus), &frame) == RF_OK);
    EXPECT(rf_frame_audio_playable(&frame) == 0);
}

static void test_face(void) {
    rf_face_t face;
    rf_face_init(&face);
    EXPECT(face.emotion == RF_EMOTION_SLEEPY);
    EXPECT(face.eye_open == 35);

    rf_face_set_emotion(&face, RF_EMOTION_NEUTRAL);
    EXPECT(face.emotion == RF_EMOTION_NEUTRAL);
    EXPECT(face.eye_open == 35);
    EXPECT(rf_face_tick(&face, 80) == 1);
    EXPECT(face.eye_open > 35 && face.eye_open < 100);
    EXPECT(rf_face_tick(&face, 400) == 1);
    EXPECT(face.eye_open >= 95);

    face.blink_interval_ms = 1000;
    face.blink_hold_ms = 200;
    face.ms_since_blink = 0;
    face.blinking = 0;
    face.eye_q = 1000;
    face.eye_from = 1000;
    face.eye_to = 1000;
    face.eye_open = 100;
    EXPECT(rf_face_tick(&face, 1000) == 1);
    EXPECT(face.blinking == 1);
    EXPECT(rf_face_tick(&face, 100) == 1);
    EXPECT(face.eye_open < 20);
    EXPECT(rf_face_tick(&face, 100) == 1);
    EXPECT(face.blinking == 0);
    EXPECT(face.eye_open > 80);

    EXPECT(rf_face_emotion_from_text("Great job") == RF_EMOTION_HAPPY);
    EXPECT(rf_face_emotion_from_text("I cannot do that") == RF_EMOTION_SAD);
    EXPECT(rf_face_emotion_from_text("好的") == RF_EMOTION_HAPPY);
    EXPECT(rf_face_emotion_from_text("抱歉") == RF_EMOTION_SAD);
    EXPECT(rf_face_emotion_from_text("hello there") == RF_EMOTION_NEUTRAL);
    EXPECT(strcmp(rf_face_emotion_name(RF_EMOTION_WORRIED), "worried") == 0);

    rf_face_set_emotion(&face, RF_EMOTION_NEUTRAL);
    rf_face_set_cue(&face, RF_CUE_NONE);
    rf_face_tick(&face, RF_FACE_LERP_MS);
    face.blinking = 0;
    face.eye_open = 100;
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

    rf_face_set_emotion(&face, RF_EMOTION_HAPPY);
    rf_face_set_cue(&face, RF_CUE_NONE);
    rf_face_tick(&face, RF_FACE_LERP_MS);
    face.blinking = 0;
    rf_face_render_rgb565(&face, happy_px, RF_FACE_W, RF_FACE_H);
    rf_face_set_emotion(&face, RF_EMOTION_SAD);
    rf_face_tick(&face, RF_FACE_LERP_MS);
    face.blinking = 0;
    rf_face_render_rgb565(&face, sad_px, RF_FACE_W, RF_FACE_H);
    EXPECT(checksum(happy_px, RF_FACE_W * RF_FACE_H) != checksum(sad_px, RF_FACE_W * RF_FACE_H));
    EXPECT(count_color(happy_px, RF_FACE_W * RF_FACE_H, RF_FACE_COLOR_EAR) > 0);
    EXPECT(count_color(happy_px, RF_FACE_W * RF_FACE_H, RF_FACE_COLOR_NOSE) > 0);
    EXPECT(count_color(happy_px, RF_FACE_W * RF_FACE_H, RF_FACE_COLOR_MOUTH) > 0);
    EXPECT(rf_face_render_rgb565(NULL, open_px, 8, 8) == -1);

    static uint16_t stripe[RF_FACE_W * 16];
    for (int y = 0; y < RF_FACE_H;) {
        int rows = 16;
        if (y + rows > RF_FACE_H) {
            rows = RF_FACE_H - y;
        }
        EXPECT(rf_face_render_stripe(&face, stripe, RF_FACE_W, y, rows, RF_FACE_H) == 0);
        EXPECT(memcmp(stripe, sad_px + y * RF_FACE_W, (size_t)rows * RF_FACE_W * sizeof(uint16_t)) == 0);
        y += rows;
    }

    static uint16_t wide[480 * 16];
    rf_face_init(&face);
    rf_face_set_emotion(&face, RF_EMOTION_NEUTRAL);
    rf_face_tick(&face, RF_FACE_LERP_MS);
    face.eye_open = 100;
    face.blinking = 0;
    EXPECT(rf_face_render_stripe(&face, wide, 480, 0, 16, 480) == 0);
    EXPECT(count_color(wide, 480 * 16, RF_FACE_COLOR_EAR) > 200);
    EXPECT(rf_face_render_stripe(&face, wide, 480, 250, 16, 480) == 0);
    int fur = count_color(wide, 480 * 16, RF_FACE_COLOR_FUR);
    EXPECT(fur > 480 * 16 / 2);

    rf_face_set_cue(&face, RF_CUE_LISTENING);
    rf_face_tick(&face, RF_FACE_LERP_MS);
    EXPECT(face.perk_q > 700);
    rf_face_set_cue(&face, RF_CUE_SPEAKING);
    rf_face_tick(&face, RF_FACE_LERP_MS);
    EXPECT(face.mouth_q > 400);
    rf_face_set_cue(&face, RF_CUE_HOLD_CAP);
    rf_face_tick(&face, RF_FACE_LERP_MS);
    face.emotion = RF_EMOTION_NEUTRAL;
    EXPECT(rf_face_render_stripe(&face, wide, 480, 8, 16, 480) == 0);
    EXPECT(count_color(wide, 480 * 16, RF_FACE_COLOR_CUE) > 20);

    rf_face_set_emotion(&face, RF_EMOTION_WORRIED);
    rf_face_set_cue(&face, RF_CUE_LISTENING);
    rf_face_tick(&face, RF_FACE_LERP_MS);
    EXPECT(face.worry_q > 700);
    EXPECT(face.perk_q < 100);
    EXPECT(rf_face_render_stripe(&face, wide, 480, 0, 16, 480) == 0);
    EXPECT(count_color(wide, 480 * 16, RF_FACE_COLOR_ALERT) > 20);
    EXPECT(count_color(wide, 480 * 16, RF_FACE_COLOR_CUE) == 0);
}

static void test_talk(void) {
    rf_talk_t talk;
    rf_talk_init(&talk);
    rf_talk_set_link(&talk, 1);
    rf_talk_press(&talk);
    EXPECT(talk.phase == RF_TALK_HOLDING);
    rf_talk_tick(&talk, 200);
    rf_talk_release(&talk);
    EXPECT(talk.phase == RF_TALK_DISCARD);
    rf_talk_finish(&talk);

    rf_talk_set_link(&talk, 1);
    rf_talk_press(&talk);
    rf_talk_tick(&talk, 400);
    rf_talk_release(&talk);
    EXPECT(talk.phase == RF_TALK_SEND);
    EXPECT(talk.cap_cue == 0);
    rf_talk_finish(&talk);

    rf_talk_set_link(&talk, 1);
    rf_talk_press(&talk);
    rf_talk_tick(&talk, RF_TALK_MAX_HOLD_MS);
    EXPECT(talk.phase == RF_TALK_SEND);
    EXPECT(talk.cap_cue == 1);
    rf_talk_release(&talk);
    EXPECT(talk.phase == RF_TALK_SEND);
    rf_talk_finish(&talk);

    rf_talk_set_link(&talk, 1);
    rf_talk_set_playing(&talk, 1);
    rf_talk_press(&talk);
    EXPECT(talk.barge_in == 1);
    EXPECT(talk.playing == 0);
    EXPECT(talk.phase == RF_TALK_HOLDING);
    rf_talk_set_link(&talk, 0);
    EXPECT(talk.phase == RF_TALK_DISCARD);
    rf_talk_finish(&talk);

    rf_talk_set_link(&talk, 0);
    rf_talk_press(&talk);
    EXPECT(talk.phase == RF_TALK_DISCARD);

    uint8_t report[10] = {0};
    report[5] = 1;
    report[6] = RF_CST_ACK;
    EXPECT(rf_cst_frame_down(report, sizeof(report)) == 1);
    report[6] = 0;
    EXPECT(rf_cst_frame_down(report, sizeof(report)) == 0);
    report[6] = RF_CST_ACK;
    report[5] = 0;
    EXPECT(rf_cst_frame_down(report, sizeof(report)) == 0);
    EXPECT(rf_cst_kind_from_id(RF_CST_CHIP_CST9220) == 1);
    EXPECT(rf_cst_kind_from_id(RF_CST_CHIP_CST9217) == 2);
    EXPECT(rf_cst_kind_from_id(0x816) == 0);

    uint8_t wav[RF_WAV_HEADER_BYTES];
    rf_wav_write_header(wav, 16000, 32000);
    EXPECT(memcmp(wav, "RIFF", 4) == 0);
    EXPECT(memcmp(wav + 8, "WAVE", 4) == 0);
    EXPECT(memcmp(wav + 36, "data", 4) == 0);
    EXPECT(wav[22] == 1 && wav[23] == 0);
    EXPECT(wav[24] == 0x80 && wav[25] == 0x3e);
    EXPECT(wav[34] == 16 && wav[35] == 0);
    EXPECT(wav[40] == 0x00 && wav[41] == 0x7d && wav[42] == 0 && wav[43] == 0);

    char origin[64];
    EXPECT(rf_audio_origin_from_ws("ws://192.168.1.10:8080/api/device/v1/ws", origin, sizeof(origin)) == 0);
    EXPECT(strcmp(origin, "http://192.168.1.10:8080") == 0);
    EXPECT(rf_audio_origin_from_ws("wss://fox.example/api/device/v1/ws", origin, sizeof(origin)) == 0);
    EXPECT(strcmp(origin, "https://fox.example") == 0);
    EXPECT(rf_audio_origin_from_ws("http://192.168.1.10/api/device/v1/ws", origin, sizeof(origin)) != 0);
    EXPECT(rf_audio_path_ok("/api/device/v1/audio/abc") == 1);
    EXPECT(rf_audio_path_ok("/api/device/v1/audio/") == 0);
    EXPECT(rf_audio_path_ok("https://other/api/device/v1/audio/abc") == 0);
}

int main(void) {
    test_frames();
    test_face();
    test_talk();
    if (g_failed) {
        fprintf(stderr, "%d assertion(s) failed\n", g_failed);
        return 1;
    }
    printf("host tests ok\n");
    return 0;
}
