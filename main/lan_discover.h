// SPDX-License-Identifier: MIT
// Copyright (c) 2026 chinkan.ai

#pragma once

#include <stddef.h>

/* Start the mDNS client once Wi-Fi has an address. Safe to call again. */
void lan_discover_prepare(void);

/* 1 when NVS holds a connectable ws:// URI. Nothing else is read. */
int lan_discover_load(char *dst, size_t dst_len);

/* Store that URI. Refuses anything that is not a connectable discovered URI. */
int lan_discover_store(const char *uri);

/* Browse _rustfox-device._tcp. Writes one URI, or returns -1. */
int lan_discover_browse(char *dst, size_t dst_len);
