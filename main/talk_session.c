// SPDX-License-Identifier: MIT
// Copyright (c) 2026 chinkan.ai
//
// Press-and-hold captures mono PCM into the audio flash partition (not a
// SRAM clip), then uploads one WAV. Playback streams PCM and stops if the
// finger goes down again.

#include "talk_session.h"

#include "board_audio.h"
#include "device_link.h"
#include "face_view.h"
#include "rf_talk.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "rf_talk";

#define REC_FRAMES 256
#define PCM_MAX_BYTES (RF_AUDIO_RATE * 2 * (RF_TALK_MAX_HOLD_MS / 1000))

static TaskHandle_t s_task;
static SemaphoreHandle_t s_mu;
static const esp_partition_t *s_part;
static size_t s_erased;

static volatile int s_finger;
static volatile int s_stop_play;
static volatile int s_playing;
static volatile int s_barge;
static volatile int s_pending;
static volatile uint32_t s_press_seq;
static volatile uint32_t s_epoch = 1;
static volatile uint32_t s_upload_epoch;
static volatile uint32_t s_listen_epoch;
static volatile uint32_t s_audio_epoch;
static char s_url[160];

static int link_ok(void) {
    return device_link_session_ok();
}

static void on_audio_url(const char *url) {
    if (!url || !s_mu) {
        return;
    }
    if (xSemaphoreTake(s_mu, pdMS_TO_TICKS(20)) != pdTRUE) {
        return;
    }
    if (s_listen_epoch == 0 || s_epoch != s_listen_epoch) {
        xSemaphoreGive(s_mu);
        return;
    }
    snprintf(s_url, sizeof(s_url), "%s", url);
    s_audio_epoch = s_epoch;
    s_pending = 1;
    xSemaphoreGive(s_mu);
    if (s_task) {
        xTaskNotifyGive(s_task);
    }
}

static void arm_listen(uint32_t epoch) {
    if (!s_mu || xSemaphoreTake(s_mu, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }
    if (s_epoch == epoch) {
        s_listen_epoch = epoch;
    }
    xSemaphoreGive(s_mu);
}

static void disarm_listen(uint32_t epoch) {
    if (!s_mu || xSemaphoreTake(s_mu, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }
    if (s_listen_epoch == epoch) {
        s_listen_epoch = 0;
    }
    if (s_audio_epoch == epoch) {
        s_pending = 0;
    }
    xSemaphoreGive(s_mu);
}

void talk_session_on_contact(int down) {
    int was = s_finger;
    s_finger = down ? 1 : 0;
    if (down && !was) {
        if (s_playing) {
            s_barge = 1;
        }
        s_stop_play = 1;
        s_epoch++;
        if (s_mu && xSemaphoreTake(s_mu, pdMS_TO_TICKS(20)) == pdTRUE) {
            s_pending = 0;
            s_listen_epoch = 0;
            xSemaphoreGive(s_mu);
        } else {
            s_pending = 0;
            s_listen_epoch = 0;
        }
        board_audio_stop_playback();
        s_press_seq++;
        if (s_task) {
            xTaskNotifyGive(s_task);
        }
    }
}

static esp_err_t flash_write(size_t off, const void *data, size_t n) {
    if (!s_part || off + n > s_part->size) {
        return ESP_ERR_NO_MEM;
    }
    size_t end = off + n;
    size_t sec_end = (end + 4095u) & ~4095u;
    if (sec_end > s_part->size) {
        return ESP_ERR_NO_MEM;
    }
    while (s_erased < sec_end) {
        esp_err_t err = esp_partition_erase_range(s_part, s_erased, 4096);
        if (err != ESP_OK) {
            return err;
        }
        s_erased += 4096;
    }
    return esp_partition_write(s_part, off, data, n);
}

static int http_write_all(esp_http_client_handle_t client, const uint8_t *data, int len) {
    int left = len;
    const uint8_t *p = data;
    while (left > 0) {
        int n = esp_http_client_write(client, (const char *)p, left);
        if (n <= 0) {
            return -1;
        }
        p += n;
        left -= n;
    }
    return 0;
}

static int auth_header(char *dst, size_t dst_len) {
    if (!CONFIG_RUSTFOX_DEVICE_TOKEN[0]) {
        return -1;
    }
    int n = snprintf(dst, dst_len, "Bearer %s", CONFIG_RUSTFOX_DEVICE_TOKEN);
    if (n < 0 || (size_t)n >= dst_len) {
        return -1;
    }
    return 0;
}

static int origin_of(char *dst, size_t dst_len) {
    return rf_audio_origin_from_ws(CONFIG_RUSTFOX_WS_URI, dst, dst_len);
}

static void note_status(int status) {
    if (status == 401) {
        device_link_note_unauthorized();
    }
}

static int upload_wav(uint32_t epoch, size_t pcm_bytes) {
    char origin[160];
    char url[320];
    char auth[220];
    if (origin_of(origin, sizeof(origin)) != 0 || auth_header(auth, sizeof(auth)) != 0) {
        return -1;
    }
    int wrote = snprintf(url, sizeof(url), "%s%s", origin, RF_AUDIO_POST_PATH);
    if (wrote < 0 || (size_t)wrote >= sizeof(url)) {
        return -1;
    }
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
    };
    if (strncmp(url, "https://", 8) == 0) {
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
    }
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return -1;
    }
    esp_http_client_set_header(client, "Content-Type", "audio/wav");
    esp_http_client_set_header(client, "Authorization", auth);
    int body = RF_WAV_HEADER_BYTES + (int)pcm_bytes;
    esp_err_t err = esp_http_client_open(client, body);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return -1;
    }
    uint8_t header[RF_WAV_HEADER_BYTES];
    rf_wav_write_header(header, RF_AUDIO_RATE, (uint32_t)pcm_bytes);
    int ok = http_write_all(client, header, RF_WAV_HEADER_BYTES) == 0;
    uint8_t chunk[1024];
    size_t off = 0;
    while (ok && off < pcm_bytes) {
        if (s_epoch != epoch || !link_ok()) {
            ok = 0;
            break;
        }
        size_t n = pcm_bytes - off;
        if (n > sizeof(chunk)) {
            n = sizeof(chunk);
        }
        if (esp_partition_read(s_part, off, chunk, n) != ESP_OK || http_write_all(client, chunk, (int)n) != 0) {
            ok = 0;
            break;
        }
        off += n;
    }
    int status = 0;
    if (ok) {
        if (esp_http_client_fetch_headers(client) < 0) {
            ok = 0;
        } else {
            status = esp_http_client_get_status_code(client);
            ok = status >= 200 && status < 300;
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (!ok) {
        ESP_LOGW(TAG, "audio upload failed status=%d", status);
        note_status(status);
        return -1;
    }
    ESP_LOGI(TAG, "uploaded %u pcm bytes", (unsigned)pcm_bytes);
    return 0;
}

static void play_url(const char *path, uint32_t epoch) {
    char origin[160];
    char url[320];
    char auth[220];
    if (origin_of(origin, sizeof(origin)) != 0 || auth_header(auth, sizeof(auth)) != 0) {
        return;
    }
    if (!rf_audio_path_ok(path)) {
        return;
    }
    int wrote = snprintf(url, sizeof(url), "%s%s", origin, path);
    if (wrote < 0 || (size_t)wrote >= sizeof(url)) {
        return;
    }
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
        .buffer_size = 1024,
    };
    if (strncmp(url, "https://", 8) == 0) {
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
    }
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return;
    }
    esp_http_client_set_header(client, "Authorization", auth);
    if (esp_http_client_open(client, 0) != ESP_OK) {
        esp_http_client_cleanup(client);
        return;
    }
    if (esp_http_client_fetch_headers(client) < 0) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return;
    }
    int status = esp_http_client_get_status_code(client);
    if (status < 200 || status >= 300) {
        ESP_LOGW(TAG, "audio download status=%d", status);
        note_status(status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return;
    }
    esp_http_client_set_timeout_ms(client, 200);
    s_playing = 1;
    s_stop_play = 0;
    face_view_set_cue(RF_CUE_SPEAKING);
    uint8_t raw[512];
    int16_t mono[256];
    while (s_epoch == epoch && link_ok() && !s_stop_play) {
        int n = esp_http_client_read(client, (char *)raw, sizeof(raw));
        if (n < 0) {
            break;
        }
        if (n == 0) {
            if (esp_http_client_is_complete_data_received(client) || s_stop_play) {
                break;
            }
            continue;
        }
        size_t samples = (size_t)n / 2;
        while (samples > 0 && s_epoch == epoch && !s_stop_play) {
            size_t take = samples > 256 ? 256 : samples;
            memcpy(mono, raw + ((size_t)n / 2 - samples) * 2, take * 2);
            if (board_audio_write_mono(mono, take) != ESP_OK) {
                samples = 0;
                break;
            }
            samples -= take;
        }
    }
    s_playing = 0;
    if (s_stop_play || s_epoch != epoch) {
        board_audio_stop_playback();
    }
    face_view_set_cue(RF_CUE_NONE);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}

static void maybe_play(void) {
    char path[160];
    uint32_t ep = 0;
    if (!s_mu) {
        return;
    }
    if (xSemaphoreTake(s_mu, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }
    int fresh = s_pending && s_audio_epoch == s_epoch && s_audio_epoch == s_listen_epoch && link_ok();
    int go = fresh && s_upload_epoch == s_audio_epoch && s_upload_epoch != 0;
    if (s_pending && !fresh) {
        s_pending = 0;
    }
    if (go) {
        snprintf(path, sizeof(path), "%s", s_url);
        ep = s_audio_epoch;
        s_pending = 0;
    }
    xSemaphoreGive(s_mu);
    if (go) {
        play_url(path, ep);
    }
}

static void run_hold(void) {
    uint32_t epoch = s_epoch;
    rf_talk_t talk;
    rf_talk_init(&talk);
    rf_talk_set_link(&talk, link_ok());
    rf_talk_set_playing(&talk, s_barge);
    rf_talk_press(&talk);
    if (talk.barge_in) {
        ESP_LOGI(TAG, "barge-in; playback stopped");
    }
    s_barge = 0;
    s_playing = 0;
    if (talk.phase == RF_TALK_DISCARD) {
        face_view_set(RF_EMOTION_WORRIED);
        face_view_set_cue(RF_CUE_NONE);
        ESP_LOGI(TAG, "hold discarded (link down)");
        rf_talk_finish(&talk);
        return;
    }
    face_view_set_cue(RF_CUE_LISTENING);
    s_erased = 0;
    size_t pcm = 0;
    size_t cap = PCM_MAX_BYTES;
    if (s_part && s_part->size < cap) {
        cap = s_part->size;
    }
    int64_t last = esp_timer_get_time();
    int16_t mono[REC_FRAMES];
    while (talk.phase == RF_TALK_HOLDING) {
        int64_t now = esp_timer_get_time();
        uint32_t dt = (uint32_t)((now - last) / 1000);
        last = now;
        if (s_epoch != epoch) {
            rf_talk_set_link(&talk, 0);
        } else {
            rf_talk_set_link(&talk, link_ok());
        }
        rf_talk_tick(&talk, dt);
        if (!s_finger) {
            rf_talk_release(&talk);
        }
        if (talk.phase != RF_TALK_HOLDING) {
            break;
        }
        if (!s_part || board_audio_read_mono(mono, REC_FRAMES) != ESP_OK) {
            talk.phase = RF_TALK_DISCARD;
            break;
        }
        size_t nbytes = REC_FRAMES * 2;
        if (pcm + nbytes > cap) {
            talk.phase = RF_TALK_SEND;
            talk.cap_cue = 1;
            break;
        }
        if (flash_write(pcm, mono, nbytes) != ESP_OK) {
            talk.phase = RF_TALK_DISCARD;
            break;
        }
        pcm += nbytes;
    }
    int link_lost = !link_ok() || s_epoch != epoch;
    if (talk.phase != RF_TALK_SEND || pcm == 0 || link_lost) {
        if (link_lost) {
            face_view_set(RF_EMOTION_WORRIED);
        }
        face_view_set_cue(RF_CUE_NONE);
        ESP_LOGI(TAG, "recording discarded (%u bytes)", (unsigned)pcm);
        rf_talk_finish(&talk);
        return;
    }
    if (talk.cap_cue) {
        face_view_set_cue(RF_CUE_HOLD_CAP);
        ESP_LOGI(TAG, "hold cap, sending %u bytes", (unsigned)pcm);
    }
    arm_listen(epoch);
    if (upload_wav(epoch, pcm) == 0 && s_epoch == epoch && link_ok()) {
        s_upload_epoch = epoch;
        face_view_set_cue(RF_CUE_NONE);
        face_view_set(RF_EMOTION_THINKING);
    } else {
        disarm_listen(epoch);
        face_view_set_cue(RF_CUE_NONE);
        face_view_set(RF_EMOTION_WORRIED);
    }
    rf_talk_finish(&talk);
}

static void talk_task(void *arg) {
    (void)arg;
    uint32_t seen = s_press_seq;
    int was_link = link_ok();
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));
        int ok = link_ok();
        if (was_link && !ok) {
            s_epoch++;
            if (s_mu && xSemaphoreTake(s_mu, pdMS_TO_TICKS(20)) == pdTRUE) {
                s_pending = 0;
                s_listen_epoch = 0;
                xSemaphoreGive(s_mu);
            } else {
                s_pending = 0;
                s_listen_epoch = 0;
            }
            s_stop_play = 1;
            board_audio_stop_playback();
        }
        was_link = ok;
        uint32_t seq = s_press_seq;
        if (seq != seen) {
            seen = seq;
            run_hold();
        } else {
            maybe_play();
        }
    }
}

void talk_session_start(void) {
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "audio");
    if (!s_part) {
        ESP_LOGE(TAG, "audio partition missing");
        return;
    }
    s_mu = xSemaphoreCreateMutex();
    if (!s_mu) {
        ESP_LOGE(TAG, "mutex failed");
        return;
    }
    device_link_set_audio_fn(on_audio_url);
    if (xTaskCreate(talk_task, "rf_talk_io", 20480, NULL, 5, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "task failed");
        return;
    }
    ESP_LOGI(TAG, "press-and-hold ready, audio partition %u bytes", (unsigned)s_part->size);
}
