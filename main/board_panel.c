// SPDX-License-Identifier: MIT
// Copyright (c) 2026 chinkan.ai
//
// Panel power-on and the QSPI init command list are adapted from
// 78/xiaozhi-esp32 main/boards/waveshare/esp32-c6-touch-amoled-2.16
// (revision a73e9a7e266e3196d1368ba46c30bee0d81f0a64), which is MIT.
// Copyright (c) 2025 Shenzhen Xinzhi Future Technology Co., Ltd.
// Copyright (c) 2025 Project Contributors
//
// The Waveshare product page names the glass controller CO5300. That MIT
// board file drives it with the SH8601 QSPI command set. The upstream
// 0x2A/0x2B window is 410x502 (the 2.06" geometry). This file programs
// 0x0000..0x01DF for the published 480x480 panel. Confirm on the bench.

#include "board_panel.h"

#include "board_pins.h"

#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_sh8601.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "rf_panel";

static i2c_master_bus_handle_t s_i2c;
static i2c_master_dev_handle_t s_axp;
static esp_lcd_panel_handle_t s_panel;
static int s_ready;
static int s_axp_ok;

static const uint8_t k_00[] = {0x00};
static const uint8_t k_c4[] = {0x80};
static const uint8_t k_44[] = {0x01, 0xD1};
static const uint8_t k_35[] = {0x00};
static const uint8_t k_36[] = {0xA0};
static const uint8_t k_53[] = {0x20};
static const uint8_t k_63[] = {0xFF};
static const uint8_t k_2a[] = {0x00, 0x00, 0x01, 0xDF};
static const uint8_t k_2b[] = {0x00, 0x00, 0x01, 0xDF};
static const uint8_t k_51[] = {0xFF};

static const sh8601_lcd_init_cmd_t k_init[] = {
    {0x11, k_00, 0, 120},
    {0xC4, k_c4, 1, 0},
    {0x44, k_44, 2, 0},
    {0x35, k_35, 1, 0},
    {0x36, k_36, 1, 0},
    {0x53, k_53, 1, 10},
    {0x63, k_63, 1, 10},
    {0x2A, k_2a, 4, 0},
    {0x2B, k_2b, 4, 0},
    {0x29, k_00, 0, 10},
    {0x51, k_51, 1, 0},
};

/* One QSPI transfer. A 480x480 RGB565 frame is not allocated. */
#define STRIPE_ROWS 16

static uint16_t s_stripe[BOARD_LCD_H_RES * STRIPE_ROWS];

static esp_err_t axp_write(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_axp, buf, sizeof(buf), 100);
}

static esp_err_t axp_read(uint8_t reg, uint8_t *val) {
    return i2c_master_transmit_receive(s_axp, &reg, 1, val, 1, 100);
}

static esp_err_t axp_bringup(void) {
    esp_err_t err;
    err = axp_write(0x22, 0x06);
    if (err != ESP_OK) {
        return err;
    }
    err = axp_write(0x27, 0x10);
    if (err != ESP_OK) {
        return err;
    }
    err = axp_write(0x80, 0x01);
    if (err != ESP_OK) {
        return err;
    }
    err = axp_write(0x90, 0x00);
    if (err != ESP_OK) {
        return err;
    }
    err = axp_write(0x91, 0x00);
    if (err != ESP_OK) {
        return err;
    }
    err = axp_write(0x82, (uint8_t)((3300 - 1500) / 100));
    if (err != ESP_OK) {
        return err;
    }
    uint8_t aldo = (uint8_t)((3300 - 500) / 100);
    err = axp_write(0x92, aldo);
    if (err != ESP_OK) {
        return err;
    }
    err = axp_write(0x93, aldo);
    if (err != ESP_OK) {
        return err;
    }
    err = axp_write(0x94, aldo);
    if (err != ESP_OK) {
        return err;
    }
    err = axp_write(0x95, aldo);
    if (err != ESP_OK) {
        return err;
    }
    err = axp_write(0x90, 0x0F);
    if (err != ESP_OK) {
        return err;
    }
    err = axp_write(0x64, 0x02);
    if (err != ESP_OK) {
        return err;
    }
    err = axp_write(0x61, 0x02);
    if (err != ESP_OK) {
        return err;
    }
    err = axp_write(0x62, 0x0A);
    if (err != ESP_OK) {
        return err;
    }
    return axp_write(0x63, 0x01);
}

static esp_err_t axp_set_aldo3(int enable) {
    uint8_t reg = 0;
    esp_err_t err = axp_read(0x90, &reg);
    if (err != ESP_OK) {
        return err;
    }
    if (enable) {
        reg |= 0x04;
    } else {
        reg &= (uint8_t)~0x04;
    }
    return axp_write(0x90, reg);
}

static esp_err_t panel_power_cycle(void) {
    esp_err_t err = axp_set_aldo3(1);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    err = axp_set_aldo3(0);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    err = axp_set_aldo3(1);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}

static esp_err_t clear_panel(void) {
    memset(s_stripe, 0, sizeof(s_stripe));
    for (int y = 0; y < BOARD_LCD_V_RES; y += STRIPE_ROWS) {
        int rows = STRIPE_ROWS;
        if (y + rows > BOARD_LCD_V_RES) {
            rows = BOARD_LCD_V_RES - y;
        }
        esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, 0, y, BOARD_LCD_H_RES, y + rows, s_stripe);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

i2c_master_bus_handle_t board_i2c_bus(void) {
    return s_i2c;
}

esp_err_t board_i2c_init(void) {
    if (s_i2c) {
        return s_axp_ok ? ESP_OK : ESP_FAIL;
    }
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = 1,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_i2c);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c bus failed: %s", esp_err_to_name(err));
        s_i2c = NULL;
        return err;
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BOARD_AXP2101_ADDR,
        .scl_speed_hz = 400000,
    };
    err = i2c_master_bus_add_device(s_i2c, &dev_cfg, &s_axp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "axp2101 add failed: %s", esp_err_to_name(err));
        return err;
    }
    err = axp_bringup();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "axp2101 did not ack at 0x%02x (%s)", BOARD_AXP2101_ADDR, esp_err_to_name(err));
        return err;
    }
    s_axp_ok = 1;
    ESP_LOGI(TAG, "axp2101 rails on");
    return ESP_OK;
}

esp_err_t board_panel_init(void) {
#if !CONFIG_RUSTFOX_ENABLE_PANEL
    ESP_LOGI(TAG, "panel bring-up disabled");
    return ESP_ERR_NOT_SUPPORTED;
#else
    esp_err_t err = board_i2c_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "panel skipped; i2c/pmic not ready");
        return err;
    }

    const spi_bus_config_t buscfg = SH8601_PANEL_BUS_QSPI_CONFIG(
        BOARD_LCD_PCLK, BOARD_LCD_D0, BOARD_LCD_D1, BOARD_LCD_D2, BOARD_LCD_D3,
        BOARD_LCD_H_RES * STRIPE_ROWS * (int)sizeof(uint16_t));
    err = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "qspi bus failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_panel_io_handle_t io = NULL;
    const esp_lcd_panel_io_spi_config_t io_cfg = SH8601_PANEL_IO_QSPI_CONFIG(BOARD_LCD_CS, NULL, NULL);
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel io failed: %s", esp_err_to_name(err));
        return err;
    }

    const sh8601_vendor_config_t vendor = {
        .init_cmds = k_init,
        .init_cmds_size = sizeof(k_init) / sizeof(k_init[0]),
        .flags = {.use_qspi_interface = 1},
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = BOARD_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = (void *)&vendor,
    };
    err = esp_lcd_new_panel_sh8601(io, &panel_cfg, &s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sh8601 create failed: %s", esp_err_to_name(err));
        return err;
    }
    err = panel_power_cycle();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "aldo3 reset failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_lcd_panel_init(s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel init failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_lcd_panel_disp_on_off(s_panel, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "display on returned %s", esp_err_to_name(err));
    }
    err = clear_panel();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "clear failed: %s", esp_err_to_name(err));
        return err;
    }
    s_ready = 1;
    ESP_LOGI(TAG, "amoled ready %dx%d (stripe blit, no full framebuffer)", BOARD_LCD_H_RES,
             BOARD_LCD_V_RES);
    return ESP_OK;
#endif
}

void board_panel_draw(const rf_face_t *face) {
    if (!s_ready || !face || !s_panel) {
        return;
    }
    for (int y = 0; y < BOARD_LCD_V_RES; y += STRIPE_ROWS) {
        int rows = STRIPE_ROWS;
        if (y + rows > BOARD_LCD_V_RES) {
            rows = BOARD_LCD_V_RES - y;
        }
        if (rf_face_render_stripe(face, s_stripe, BOARD_LCD_H_RES, y, rows, BOARD_LCD_V_RES) != 0) {
            return;
        }
        esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, 0, y, BOARD_LCD_H_RES, y + rows, s_stripe);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "blit failed: %s", esp_err_to_name(err));
            return;
        }
    }
}
