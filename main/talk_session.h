// SPDX-License-Identifier: MIT
// Copyright (c) 2026 chinkan.ai

#pragma once

/* Finger down (1) or up (0). Called from the touch task. */
void talk_session_on_contact(int down);

/* Record task, flash clip, WAV upload, PCM playback. */
void talk_session_start(void);
