// SPDX-License-Identifier: MIT
// Copyright (c) 2026 chinkan.ai

#pragma once

#include "esp_err.h"

/* Block until associated or timeout_ms elapses. */
esp_err_t wifi_sta_connect(const char *ssid, const char *password, uint32_t timeout_ms);
