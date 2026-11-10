// SPDX-License-Identifier: MIT
// Copyright (c) 2026 chinkan.ai

#pragma once

/* Create the send queue. Safe to call before Wi-Fi is up. */
void device_link_init(void);

/* Open /api/device/v1/ws. Requires device_link_init and an associated STA. */
void device_link_connect(void);

/* Latest manual line wins. Sent after the socket is authenticated. */
int device_link_submit_text(const char *text);
