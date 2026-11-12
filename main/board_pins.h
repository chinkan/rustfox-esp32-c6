// SPDX-License-Identifier: MIT
// Copyright (c) 2026 chinkan.ai
//
// Pin assignments adapted from 78/xiaozhi-esp32
// main/boards/waveshare/esp32-c6-touch-amoled-2.16/config.h
// (revision a73e9a7e266e3196d1368ba46c30bee0d81f0a64).
// Copyright (c) 2025 Shenzhen Xinzhi Future Technology Co., Ltd.
// Copyright (c) 2025 Project Contributors
//
// GPIO12 / GPIO13 are USB D-/D+ on ESP32-C6 and are not used here.

#pragma once

#include "driver/gpio.h"

#define BOARD_TARGET_NAME "waveshare-esp32-c6-touch-amoled-2.16"

#define BOARD_LCD_H_RES 480
#define BOARD_LCD_V_RES 480

#define BOARD_LCD_CS GPIO_NUM_15
#define BOARD_LCD_PCLK GPIO_NUM_0
#define BOARD_LCD_D0 GPIO_NUM_1
#define BOARD_LCD_D1 GPIO_NUM_2
#define BOARD_LCD_D2 GPIO_NUM_3
#define BOARD_LCD_D3 GPIO_NUM_4
#define BOARD_LCD_RST GPIO_NUM_NC

#define BOARD_I2C_SDA GPIO_NUM_8
#define BOARD_I2C_SCL GPIO_NUM_7
#define BOARD_AXP2101_ADDR 0x34

#define BOARD_TP_RST GPIO_NUM_11
#define BOARD_TP_INT GPIO_NUM_5

#define BOARD_BOOT_GPIO GPIO_NUM_9

#define BOARD_I2S_MCLK GPIO_NUM_19
#define BOARD_I2S_BCLK GPIO_NUM_20
#define BOARD_I2S_WS GPIO_NUM_22
#define BOARD_I2S_DIN GPIO_NUM_21
#define BOARD_I2S_DOUT GPIO_NUM_23
/* ES8311 playback and ES7210 capture share BOARD_I2C_*. */
