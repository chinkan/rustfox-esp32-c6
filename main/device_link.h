// SPDX-License-Identifier: MIT
// Copyright (c) 2026 chinkan.ai

#pragma once

/* Create the send queue. Safe to call before Wi-Fi is up. */
void device_link_init(void);

/* Open /api/device/v1/ws. Requires device_link_init and an associated STA. */
void device_link_connect(void);

/* Latest manual line wins. Sent after the socket is authenticated. */
int device_link_submit_text(const char *text);

/* 1 when the socket is authenticated and not in the unauthorized stop. */
int device_link_session_ok(void);

/* HTTP 401 or an unauthorized frame: worried face, stop reconnecting. */
void device_link_note_unauthorized(void);

/* assistant_audio path, after the frame is checked playable. Not called
 * from an ISR. The pointer is only valid for the duration of the call. */
typedef void (*device_link_audio_fn)(const char *url);
void device_link_set_audio_fn(device_link_audio_fn fn);
