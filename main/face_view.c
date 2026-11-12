// SPDX-License-Identifier: MIT
// Copyright (c) 2026 chinkan.ai

#include "face_view.h"

#include "board_panel.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "rf_face";

static rf_face_t s_face;
static SemaphoreHandle_t s_mu;
static int s_dirty = 1;

static void face_task(void *arg) {
    (void)arg;
    rf_emotion_t logged = RF_EMOTION_COUNT;
    for (;;) {
        rf_face_t snap;
        int draw = 0;
        xSemaphoreTake(s_mu, portMAX_DELAY);
        int changed = rf_face_tick(&s_face, 50);
        if (changed || s_dirty) {
            draw = 1;
            s_dirty = 0;
        }
        snap = s_face;
        xSemaphoreGive(s_mu);
        if (draw) {
            if (snap.emotion != logged) {
                char art[192];
                if (rf_face_format(&snap, art, sizeof(art)) == 0) {
                    ESP_LOGI(TAG, "%s", art);
                }
                logged = snap.emotion;
            } else {
                ESP_LOGD(TAG, "blink eye=%u", (unsigned)snap.eye_open);
            }
            board_panel_draw(&snap);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void face_view_start(void) {
    rf_face_init(&s_face);
    s_dirty = 1;
    s_mu = xSemaphoreCreateMutex();
    if (!s_mu) {
        ESP_LOGE(TAG, "mutex failed");
        return;
    }
    if (xTaskCreate(face_task, "rf_face", 8192, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "task failed");
    }
}

void face_view_set(rf_emotion_t emotion) {
    if (!s_mu) {
        return;
    }
    xSemaphoreTake(s_mu, portMAX_DELAY);
    rf_face_set_emotion(&s_face, emotion);
    s_dirty = 1;
    xSemaphoreGive(s_mu);
}

void face_view_set_cue(rf_cue_t cue) {
    if (!s_mu) {
        return;
    }
    xSemaphoreTake(s_mu, portMAX_DELAY);
    rf_face_set_cue(&s_face, cue);
    s_dirty = 1;
    xSemaphoreGive(s_mu);
}
