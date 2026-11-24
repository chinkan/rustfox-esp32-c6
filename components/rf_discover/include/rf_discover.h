// SPDX-License-Identifier: MIT
// Copyright (c) 2026 chinkan.ai
//
// LAN DNS-SD choice for the Device WebSocket. No sockets, NVS, or mDNS here.
// The firmware browse stores only the ws:// URI this module builds.

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RF_DISCOVER_SERVICE "_rustfox-device"
#define RF_DISCOVER_PROTO "_tcp"
#define RF_DISCOVER_INSTANCE "rustfox"
#define RF_DISCOVER_PATH "/api/device/v1/ws"
#define RF_DISCOVER_PROTO_TXT "1"
#define RF_DISCOVER_AUDIO_PATH "/api/device/v1/audio"
#define RF_DISCOVER_URI_CAP 160

/* 1 when dotted is a connectable IPv4. Rejects loopback, APIPA, Tailscale
 * 100.x, unspecified, and multicast. A 172.x address is allowed; an
 * unreachable WSL or Hyper-V address fails when the socket connect fails. */
int rf_discover_ipv4_ok(const char *dotted);

/* ws://<ipv4>:<port><path> for a connectable A record, a SRV port, and the
 * required path. Returns 0, or -1. */
int rf_discover_build_uri(const char *ipv4, int port, const char *path, char *dst, size_t dst_len);

/* 1 when uri is a URI this client may keep in NVS. */
int rf_discover_cached_uri_ok(const char *uri);

typedef struct {
    const char *key;
    const char *value;
} rf_discover_kv_t;

/* Copies path, proto, and a standard audio path. Other keys, including any
 * token or secret, are ignored and not written. Returns 0 when path and proto
 * are the required values. audio is empty when that TXT key is omitted or is
 * not /api/device/v1/audio. */
int rf_discover_apply_txt(const rf_discover_kv_t *kv, size_t count, char *path, size_t path_len,
                          char *proto, size_t proto_len, char *audio, size_t audio_len);

typedef struct {
    const char *instance;
    const char **ipv4;
    size_t ipv4_count;
    int port;
    const char *path;
    const char *proto_txt;
} rf_discover_service_t;

/* Prefer configured_name when it is set and not the default instance, else
 * the instance named rustfox, else the first service with a connectable IPv4
 * and the required TXT. Returns that index, or -1. Writes the URI on success. */
int rf_discover_select(const rf_discover_service_t *items, size_t count, const char *configured_name,
                       char *uri, size_t uri_len);

typedef enum {
    RF_BROWSE_NOT_TRIED = 0,
    RF_BROWSE_FOUND,
    RF_BROWSE_EMPTY,
    RF_BROWSE_CONNECT_FAILED
} rf_browse_state_t;

typedef enum {
    RF_DISC_STEP_NVS = 1,
    RF_DISC_STEP_BROWSE,
    RF_DISC_STEP_DISCOVERED,
    RF_DISC_STEP_COMPILED
} rf_disc_step_t;

/* (a) NVS when present and not yet failed. (b) Browse, then the discovered
 * URI. (c) Compiled fallback when browse is empty or the discovered URI failed. */
rf_disc_step_t rf_discover_next(int have_nvs, int nvs_failed, rf_browse_state_t browse);

/* 1 only after a discovered URI connects. The compiled URI is not cached. */
int rf_discover_cache_on_success(rf_disc_step_t step);

#ifdef __cplusplus
}
#endif
