// SPDX-License-Identifier: MIT
// Copyright (c) 2026 chinkan.ai

#include "board_panel.h"
#include "board_pins.h"
#include "device_link.h"
#include "face_view.h"
#include "wifi_sta.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "rf_frames.h"
#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "rf_app";

#define LINE_CAP 384

static void button_task(void *arg) {
    (void)arg;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BOARD_BOOT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    int prev = 1;
    for (;;) {
        int level = gpio_get_level(BOARD_BOOT_GPIO);
        if (prev == 1 && level == 0) {
            vTaskDelay(pdMS_TO_TICKS(30));
            if (gpio_get_level(BOARD_BOOT_GPIO) == 0) {
                ESP_LOGI(TAG, "BOOT pressed");
                device_link_submit_text(CONFIG_RUSTFOX_CANNED_TEXT);
                while (gpio_get_level(BOARD_BOOT_GPIO) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
            }
        }
        prev = gpio_get_level(BOARD_BOOT_GPIO);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void uart_task(void *arg) {
    (void)arg;
    char line[LINE_CAP];
    for (;;) {
        if (!fgets(line, sizeof(line), stdin)) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        size_t n = strlen(line);
        int got_nl = n && (line[n - 1] == '\n' || line[n - 1] == '\r');
        if (!got_nl && n + 1 == sizeof(line)) {
            int ch;
            while ((ch = getchar()) != '\n' && ch != '\r' && ch != EOF) {
            }
            ESP_LOGW(TAG, "line longer than %u bytes dropped", (unsigned)(sizeof(line) - 1));
            continue;
        }
        device_link_submit_text(line);
    }
}

void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "RustFox ESP32-C6 client %s", CONFIG_RUSTFOX_FW_VERSION);
    ESP_LOGI(TAG, "board %s  path %s", BOARD_TARGET_NAME, RF_WS_PATH);
    ESP_LOGI(TAG, "panel QSPI cs=%d clk=%d d0=%d d1=%d d2=%d d3=%d  i2c sda=%d scl=%d",
             (int)BOARD_LCD_CS, (int)BOARD_LCD_PCLK, (int)BOARD_LCD_D0, (int)BOARD_LCD_D1,
             (int)BOARD_LCD_D2, (int)BOARD_LCD_D3, (int)BOARD_I2C_SDA, (int)BOARD_I2C_SCL);
    ESP_LOGI(TAG, "boot gpio=%d  i2s mclk=%d bclk=%d ws=%d din=%d dout=%d (audio not started)",
             (int)BOARD_BOOT_GPIO, (int)BOARD_I2S_MCLK, (int)BOARD_I2S_BCLK, (int)BOARD_I2S_WS,
             (int)BOARD_I2S_DIN, (int)BOARD_I2S_DOUT);

    face_view_start();
    device_link_init();

    err = board_panel_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "panel not available (%s); continuing", esp_err_to_name(err));
    }

#if CONFIG_RUSTFOX_BOOT_BUTTON_SENDS_TEXT
    xTaskCreate(button_task, "rf_btn", 3072, NULL, 3, NULL);
#endif
#if CONFIG_RUSTFOX_UART_TEXT
    xTaskCreate(uart_task, "rf_uart", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "type a line in the monitor and press enter to send user_text");
#endif

    if (CONFIG_RUSTFOX_WIFI_SSID[0] == '\0') {
        ESP_LOGW(TAG, "Wi-Fi SSID is empty. Set it under RustFox ESP32-C6 in menuconfig.");
        return;
    }
    face_view_set(RF_EMOTION_THINKING);
    err = wifi_sta_connect(CONFIG_RUSTFOX_WIFI_SSID, CONFIG_RUSTFOX_WIFI_PASSWORD, 20000);
    if (err != ESP_OK) {
        face_view_set(RF_EMOTION_WORRIED);
        return;
    }
    device_link_connect();
}
