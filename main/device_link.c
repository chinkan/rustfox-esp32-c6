// SPDX-License-Identifier: MIT
// Copyright (c) 2026 chinkan.ai
//
// JSON text client for ADR-0056. Binary frames are ignored.
// Sends happen on this task, not inside the websocket event callback.

#include "device_link.h"

#include "face_view.h"

#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "rf_frames.h"
#include "sdkconfig.h"

#include <string.h>

static const char *TAG = "rf_link";

#define RX_CAP 8192
#define TX_CAP 2560
#define PENDING_CAP 384
#define TOKEN_CAP 200

typedef enum {
    CMD_AUTH = 1,
    CMD_HELLO,
    CMD_USER,
    CMD_PING,
    CMD_STOP
} cmd_kind_t;

static QueueHandle_t s_q;
static SemaphoreHandle_t s_mu;
static esp_websocket_client_handle_t s_client;
static char s_header[TOKEN_CAP + 32];
static char s_pending[PENDING_CAP];
static char s_rx[RX_CAP];
static char s_tx[TX_CAP];
static int s_has_pending;
static int s_canned_done;
static volatile int s_authed;
static volatile int s_fatal;
static volatile int s_rx_drop;
static int s_started;
static device_link_audio_fn s_audio_fn;

static void show_worried(void) {
    face_view_set(RF_EMOTION_WORRIED);
    face_view_set_cue(RF_CUE_NONE);
}

static int queue_cmd(cmd_kind_t kind) {
    if (!s_q || (s_fatal && kind != CMD_STOP)) {
        return -1;
    }
    cmd_kind_t cmd = kind;
    if (xQueueSend(s_q, &cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "command queue full");
        return -1;
    }
    return 0;
}

static void handle_text(const char *json, int len) {
    static rf_frame_t frame;
    int rc = rf_parse_frame(json, (size_t)len, &frame);
    if (rc != RF_OK) {
        ESP_LOGW(TAG, "bad json text frame");
        show_worried();
        return;
    }
    switch (frame.type) {
    case RF_TYPE_DEVICE_ACK:
        ESP_LOGI(TAG, "device_ack status=%s device_id=%s room=%s", frame.status, frame.device_id,
                 frame.room);
        if (strcmp(frame.status, "authenticated") == 0) {
            s_authed = 1;
            face_view_set(RF_EMOTION_NEUTRAL);
            queue_cmd(CMD_HELLO);
        } else if (strcmp(frame.status, "hello") == 0) {
            queue_cmd(CMD_USER);
        } else if (strcmp(frame.status, "queued") == 0) {
            face_view_set(RF_EMOTION_THINKING);
        }
        break;
    case RF_TYPE_ASSISTANT_TEXT:
        if (frame.text_truncated) {
            ESP_LOGW(TAG, "assistant_text truncated to %d bytes", RF_FRAME_TEXT_CAP - 1);
        }
        ESP_LOGI(TAG, "assistant_text: %s", frame.text);
        face_view_set(rf_face_emotion_from_text(frame.text));
        break;
    case RF_TYPE_PONG:
        ESP_LOGD(TAG, "pong");
        break;
    case RF_TYPE_ERROR:
        ESP_LOGW(TAG, "error code=%s message=%s", frame.code, frame.message);
        if (strcmp(frame.code, "unauthorized") == 0) {
            device_link_note_unauthorized();
        } else if (strcmp(frame.code, "agent") == 0) {
            face_view_set(RF_EMOTION_SAD);
        } else {
            show_worried();
        }
        break;
    case RF_TYPE_ASSISTANT_AUDIO:
        if (!rf_frame_audio_playable(&frame)) {
            ESP_LOGW(TAG, "assistant_audio ignored (want 16000 Hz mono s16le)");
            break;
        }
        ESP_LOGI(TAG, "assistant_audio id=%s", frame.audio_id);
        if (s_audio_fn) {
            s_audio_fn(frame.audio_url);
        }
        break;
    default:
        ESP_LOGI(TAG, "frame type=%s", frame.type_name);
        break;
    }
}

static void on_ws(void *args, esp_event_base_t base, int32_t id, void *data) {
    (void)args;
    (void)base;
    if (id == WEBSOCKET_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "socket open");
        s_authed = 0;
        face_view_set(RF_EMOTION_THINKING);
#if CONFIG_RUSTFOX_AUTH_FIRST_FRAME
        queue_cmd(CMD_AUTH);
#endif
        return;
    }
    if (id == WEBSOCKET_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "socket closed");
        s_authed = 0;
        if (!s_fatal) {
            show_worried();
        }
        return;
    }
    if (id == WEBSOCKET_EVENT_ERROR) {
        const esp_websocket_event_data_t *ev = data;
        int status = ev ? ev->error_handle.esp_ws_handshake_status_code : 0;
        ESP_LOGW(TAG, "socket error handshake_status=%d", status);
        if (status == 401) {
            device_link_note_unauthorized();
        }
        return;
    }
    if (id != WEBSOCKET_EVENT_DATA || !data) {
        return;
    }
    const esp_websocket_event_data_t *ev = data;
    if (ev->op_code == 0x2) {
        ESP_LOGW(TAG, "binary frame ignored");
        return;
    }
    if (ev->op_code != 0x1 && ev->op_code != 0x0) {
        return;
    }
    if (!ev->data_ptr || ev->data_len < 0 || ev->payload_offset < 0) {
        return;
    }
    if (ev->op_code == 0x1 && ev->payload_offset == 0) {
        s_rx_drop = 0;
    }
    int off = ev->payload_offset;
    int n = ev->data_len;
    int total = ev->payload_len > 0 ? ev->payload_len : off + n;
    if (off > total || n > total - off || (size_t)off + (size_t)n >= sizeof(s_rx)) {
        s_rx_drop = 1;
        ESP_LOGW(TAG, "text frame exceeds %u byte client buffer", (unsigned)sizeof(s_rx) - 1);
        return;
    }
    if (!s_rx_drop && n > 0) {
        memcpy(s_rx + off, ev->data_ptr, (size_t)n);
    }
    if (!s_rx_drop && off + n >= total) {
        s_rx[off + n] = '\0';
        handle_text(s_rx, off + n);
    }
}

static int send_json(const char *json, int len) {
    if (!s_client || !esp_websocket_client_is_connected(s_client)) {
        return -1;
    }
    int n = esp_websocket_client_send_text(s_client, json, len, pdMS_TO_TICKS(5000));
    if (n < 0) {
        ESP_LOGW(TAG, "send failed");
        return -1;
    }
    return 0;
}

static void send_user(const char *text) {
    int n = rf_encode_user_text(s_tx, sizeof(s_tx), text);
    if (n < 0) {
        ESP_LOGW(TAG, "user_text not sent (%d)", n);
        return;
    }
    if (send_json(s_tx, n) == 0) {
        ESP_LOGI(TAG, "user_text sent");
        face_view_set(RF_EMOTION_THINKING);
    }
}

static void link_task(void *arg) {
    (void)arg;
    const TickType_t wait = pdMS_TO_TICKS(CONFIG_RUSTFOX_PING_INTERVAL_MS);
    for (;;) {
        cmd_kind_t cmd = 0;
        if (xQueueReceive(s_q, &cmd, wait) != pdTRUE) {
            if (s_authed && !s_fatal) {
                int n = rf_encode_ping(s_tx, sizeof(s_tx));
                if (n > 0) {
                    send_json(s_tx, n);
                }
            }
            continue;
        }
        if (cmd == CMD_STOP) {
            s_fatal = 1;
            s_authed = 0;
            if (s_client) {
                esp_websocket_client_stop(s_client);
            }
            ESP_LOGE(TAG, "stopped after unauthorized");
            continue;
        }
        if (s_fatal || !s_client) {
            continue;
        }
        if (cmd == CMD_AUTH) {
            int n = rf_encode_auth(s_tx, sizeof(s_tx), CONFIG_RUSTFOX_DEVICE_TOKEN);
            if (n > 0) {
                send_json(s_tx, n);
                ESP_LOGI(TAG, "auth frame sent");
            }
            continue;
        }
        if (cmd == CMD_HELLO) {
            int n = rf_encode_device_hello(s_tx, sizeof(s_tx), CONFIG_RUSTFOX_DEVICE_ID,
                                           CONFIG_RUSTFOX_ROOM, CONFIG_RUSTFOX_FW_VERSION);
            if (n > 0 && send_json(s_tx, n) == 0) {
                ESP_LOGI(TAG, "device_hello sent");
            }
            queue_cmd(CMD_USER);
            continue;
        }
        if (cmd == CMD_PING) {
            int n = rf_encode_ping(s_tx, sizeof(s_tx));
            if (n > 0) {
                send_json(s_tx, n);
            }
            continue;
        }
        if (cmd != CMD_USER || !s_authed) {
            continue;
        }
        char local[PENDING_CAP];
        int have = 0;
        xSemaphoreTake(s_mu, portMAX_DELAY);
        if (s_has_pending) {
            memcpy(local, s_pending, sizeof(local));
            s_has_pending = 0;
            have = 1;
            s_canned_done = 1;
        }
        xSemaphoreGive(s_mu);
        if (have) {
            send_user(local);
            continue;
        }
#if CONFIG_RUSTFOX_SEND_CANNED_ON_CONNECT
        if (!s_canned_done && CONFIG_RUSTFOX_CANNED_TEXT[0] != '\0') {
            s_canned_done = 1;
            send_user(CONFIG_RUSTFOX_CANNED_TEXT);
        }
#endif
    }
}

void device_link_init(void) {
    if (s_q) {
        return;
    }
    s_q = xQueueCreate(12, sizeof(cmd_kind_t));
    s_mu = xSemaphoreCreateMutex();
    if (!s_q || !s_mu) {
        ESP_LOGE(TAG, "queue init failed");
        return;
    }
    if (xTaskCreate(link_task, "rf_link", 6144, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "task failed");
    }
}

int device_link_submit_text(const char *text) {
    if (!text || !s_mu) {
        return -1;
    }
    while (*text == ' ' || *text == '\t') {
        text++;
    }
    size_t n = strlen(text);
    while (n && (text[n - 1] == ' ' || text[n - 1] == '\t' || text[n - 1] == '\n' ||
                 text[n - 1] == '\r')) {
        n--;
    }
    if (n == 0 || n >= PENDING_CAP) {
        ESP_LOGW(TAG, "user_text rejected (empty or longer than %d)", PENDING_CAP - 1);
        return -1;
    }
    xSemaphoreTake(s_mu, portMAX_DELAY);
    memcpy(s_pending, text, n);
    s_pending[n] = '\0';
    s_has_pending = 1;
    xSemaphoreGive(s_mu);
    queue_cmd(CMD_USER);
    return 0;
}

void device_link_connect(void) {
    if (s_started) {
        return;
    }
    const char *uri = CONFIG_RUSTFOX_WS_URI;
    const char *token = CONFIG_RUSTFOX_DEVICE_TOKEN;
    if (!s_q) {
        ESP_LOGE(TAG, "device_link_init was not called");
        return;
    }
    if (!token[0]) {
        ESP_LOGW(TAG, "no device token; socket not started");
        show_worried();
        return;
    }
    if (strlen(token) > TOKEN_CAP || strpbrk(token, "\r\n") != NULL) {
        ESP_LOGE(TAG, "device token is empty, too long, or contains CR/LF");
        show_worried();
        return;
    }
    if (!uri[0] || strstr(uri, RF_WS_PATH) == NULL) {
        ESP_LOGE(TAG, "URI must contain %s", RF_WS_PATH);
        show_worried();
        return;
    }

    esp_websocket_client_config_t cfg = {
        .uri = uri,
        .buffer_size = 4096,
        .task_stack = 6144,
        .reconnect_timeout_ms = 5000,
        .network_timeout_ms = 10000,
        .ping_interval_sec = 30,
        .user_agent = "rustfox-esp32-c6/0.1.0",
    };
#if CONFIG_RUSTFOX_AUTH_HEADER
    int wrote = snprintf(s_header, sizeof(s_header), "Authorization: Bearer %s\r\n", token);
    if (wrote < 0 || (size_t)wrote >= sizeof(s_header)) {
        ESP_LOGE(TAG, "auth header does not fit");
        return;
    }
    cfg.headers = s_header;
    ESP_LOGI(TAG, "auth via Authorization header (token not logged, %u bytes)",
             (unsigned)strlen(token));
#else
    ESP_LOGI(TAG, "auth via first frame (token not logged, %u bytes)", (unsigned)strlen(token));
#endif
    if (strncmp(uri, "wss://", 6) == 0) {
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
    }

    s_client = esp_websocket_client_init(&cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "websocket init failed");
        return;
    }
    esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, on_ws, NULL);
    esp_err_t err = esp_websocket_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "websocket start failed: %s", esp_err_to_name(err));
        return;
    }
    s_started = 1;
    ESP_LOGI(TAG, "connecting %s", uri);
}

int device_link_session_ok(void) {
    return s_authed && !s_fatal && s_client && esp_websocket_client_is_connected(s_client);
}

void device_link_note_unauthorized(void) {
    s_fatal = 1;
    s_authed = 0;
    show_worried();
    queue_cmd(CMD_STOP);
}

void device_link_set_audio_fn(device_link_audio_fn fn) {
    s_audio_fn = fn;
}
