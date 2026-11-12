// SPDX-License-Identifier: MIT
// Copyright (c) 2026 chinkan.ai
//
// CST9217 and CST9220 share I2C 0x5A. This file tries the command-page id
// read and, if that does not name either chip, the bootloader id read.
// The part number is whichever id answers. Touch data is the 0xD000 report.
// A long press is not acknowledged; writing the ack byte drops holds.

#include "board_touch.h"

#include "board_pins.h"
#include "board_panel.h"
#include "rf_talk.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "rf_touch";

#define TP_ADDR 0x5A
#define TP_REG_DATA 0xD000
#define TP_REG_CMD 0xD101
#define TP_REG_NORMAL 0xD109
#define TP_REG_INFO 0xD204

static i2c_master_dev_handle_t s_dev;
static board_touch_fn s_fn;

static esp_err_t tp_write(const uint8_t *buf, size_t n) {
    return i2c_master_transmit(s_dev, buf, n, 100);
}

static esp_err_t tp_write_then_read(const uint8_t *wr, size_t wn, uint8_t *rd, size_t rn) {
    esp_err_t err = i2c_master_transmit(s_dev, wr, wn, 100);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(2));
    return i2c_master_receive(s_dev, rd, rn, 100);
}

static void tp_reset(void) {
    gpio_set_level(BOARD_TP_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(12));
    gpio_set_level(BOARD_TP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(40));
}

static int kind_of(uint16_t chip) {
    int kind = rf_cst_kind_from_id(chip);
    if (!kind) {
        return 0;
    }
    ESP_LOGI(TAG, "touch id 0x%04x (%s)", chip, kind == 1 ? "CST9220" : "CST9217");
    return kind;
}

static int probe_command_page(void) {
    uint8_t enter[] = {0xD1, 0x01};
    if (tp_write(enter, sizeof(enter)) != ESP_OK) {
        return 0;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    uint8_t reg[2] = {(uint8_t)(TP_REG_INFO >> 8), (uint8_t)(TP_REG_INFO & 0xFF)};
    uint8_t id[4] = {0};
    if (tp_write_then_read(reg, sizeof(reg), id, sizeof(id)) != ESP_OK) {
        return 0;
    }
    uint16_t chip = (uint16_t)(id[2] | ((uint16_t)id[3] << 8));
    if (!kind_of(chip)) {
        chip = (uint16_t)(((uint16_t)id[2] << 8) | id[3]);
        if (!kind_of(chip)) {
            ESP_LOGW(TAG, "command page id bytes %02x %02x %02x %02x", id[0], id[1], id[2], id[3]);
            return 0;
        }
    }
    uint8_t normal[] = {(uint8_t)(TP_REG_NORMAL >> 8), (uint8_t)(TP_REG_NORMAL & 0xFF)};
    if (tp_write(normal, sizeof(normal)) != ESP_OK) {
        ESP_LOGW(TAG, "normal mode write failed");
    }
    return 1;
}

static int probe_bootloader(void) {
    uint8_t w[4];
    uint8_t r[4] = {0};
    w[0] = 0xA0;
    w[1] = 0x01;
    w[2] = 0xAA;
    if (tp_write(w, 3) != ESP_OK) {
        return 0;
    }
    vTaskDelay(pdMS_TO_TICKS(2));
    w[0] = 0xA0;
    w[1] = 0x02;
    if (tp_write_then_read(w, 2, r, 2) != ESP_OK) {
        return 0;
    }
    if (r[0] != 0x55 || r[1] != 0xB0) {
        return 0;
    }
    w[0] = 0xA0;
    w[1] = 0x10;
    w[2] = 1;
    if (tp_write(w, 3) != ESP_OK) {
        return 0;
    }
    w[0] = 0xA0;
    w[1] = 0x0C;
    w[2] = 0x7C;
    w[3] = 0x07;
    if (tp_write(w, 4) != ESP_OK) {
        return 0;
    }
    w[0] = 0xA0;
    w[1] = 0x04;
    w[2] = 0xE4;
    if (tp_write(w, 3) != ESP_OK) {
        return 0;
    }
    for (int t = 0; t < 30; t++) {
        w[0] = 0xA0;
        w[1] = 0x04;
        if (tp_write_then_read(w, 2, r, 1) != ESP_OK) {
            return 0;
        }
        if (r[0] == 0) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    w[0] = 0xA0;
    w[1] = 0x18;
    if (tp_write_then_read(w, 2, r, 4) != ESP_OK) {
        return 0;
    }
    uint16_t lo = (uint16_t)(r[0] | ((uint16_t)r[1] << 8));
    uint16_t hi = (uint16_t)(r[2] | ((uint16_t)r[3] << 8));
    if (!kind_of(lo) && !kind_of(hi)) {
        ESP_LOGW(TAG, "bootloader id bytes %02x %02x %02x %02x", r[0], r[1], r[2], r[3]);
        return 0;
    }
    return 1;
}

static int read_down(int prev) {
    uint8_t reg[2] = {(uint8_t)(TP_REG_DATA >> 8), (uint8_t)(TP_REG_DATA & 0xFF)};
    uint8_t data[10] = {0};
    int int_low = gpio_get_level(BOARD_TP_INT) == 0;
    if (tp_write_then_read(reg, sizeof(reg), data, sizeof(data)) != ESP_OK) {
        return int_low ? prev : 0;
    }
    if (data[6] == RF_CST_ACK) {
        return rf_cst_frame_down(data, sizeof(data));
    }
    return int_low ? prev : 0;
}

static void touch_task(void *arg) {
    (void)arg;
    int prev = 0;
    for (;;) {
        int down = read_down(prev);
        if (down != prev && s_fn) {
            s_fn(down);
        }
        prev = down;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t board_touch_start(board_touch_fn on_contact) {
    i2c_master_bus_handle_t bus = board_i2c_bus();
    if (!bus || !on_contact) {
        return ESP_ERR_INVALID_STATE;
    }
    gpio_config_t rst = {
        .pin_bit_mask = 1ULL << BOARD_TP_RST,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&rst);
    gpio_config_t intr = {
        .pin_bit_mask = 1ULL << BOARD_TP_INT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&intr);

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TP_ADDR,
        .scl_speed_hz = 400000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "touch add failed: %s", esp_err_to_name(err));
        return err;
    }

    tp_reset();
    int found = probe_command_page();
    if (!found) {
        ESP_LOGI(TAG, "command page missed; trying bootloader id");
        tp_reset();
        found = probe_bootloader();
        if (found) {
            tp_reset();
            uint8_t normal[] = {(uint8_t)(TP_REG_NORMAL >> 8), (uint8_t)(TP_REG_NORMAL & 0xFF)};
            tp_write(normal, sizeof(normal));
        }
    }
    if (!found) {
        ESP_LOGW(TAG, "neither CST9220 nor CST9217 answered at 0x%02x", TP_ADDR);
        return ESP_ERR_NOT_FOUND;
    }

    s_fn = on_contact;
    if (xTaskCreate(touch_task, "rf_touch", 3072, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "task failed");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "touch polling GPIO%d int GPIO%d", (int)BOARD_TP_RST, (int)BOARD_TP_INT);
    return ESP_OK;
}
