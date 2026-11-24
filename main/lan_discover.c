// SPDX-License-Identifier: MIT
// Copyright (c) 2026 chinkan.ai
//
// One-shot DNS-SD browse and the NVS last-good URI. TXT keys other than
// path, proto, and audio are not copied. A device token is never stored.

#include "lan_discover.h"

#include "rf_discover.h"

#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"
#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>

#if CONFIG_RUSTFOX_MDNS_DISCOVERY
#include "mdns.h"
#endif

static const char *TAG = "rf_mdns";

#define NVS_NS "rf_link"
#define NVS_KEY "ws_uri"

#if CONFIG_RUSTFOX_MDNS_DISCOVERY

#define BROWSE_MAX 8
#define ADDR_MAX 4
#define TXT_MAX 8

static int s_ready;

static int eq_key(const char *key, const char *want) {
    size_t i = 0;
    if (!key || !want) {
        return 0;
    }
    while (want[i]) {
        char c = key[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        if (c != want[i]) {
            return 0;
        }
        i++;
    }
    return key[i] == '\0';
}

static void copy_cap(char *dst, size_t dst_len, const char *src) {
    size_t n;
    if (!dst || dst_len == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    n = strlen(src);
    if (n >= dst_len) {
        n = dst_len - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void txt_value(const mdns_result_t *result, size_t index, char *dst, size_t dst_len) {
    size_t n;
    dst[0] = '\0';
    if (!result->txt || index >= result->txt_count || !result->txt[index].value) {
        return;
    }
    if (result->txt_value_len) {
        n = result->txt_value_len[index];
    } else {
        n = strlen(result->txt[index].value);
    }
    if (n >= dst_len) {
        n = dst_len - 1;
    }
    memcpy(dst, result->txt[index].value, n);
    dst[n] = '\0';
}

static void log_heap(const char *what, unsigned before) {
    ESP_LOGI(TAG, "%s free_heap %u -> %u min_free=%u", what, before, (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size());
}

#endif

void lan_discover_prepare(void) {
#if CONFIG_RUSTFOX_MDNS_DISCOVERY
    unsigned before;
    esp_err_t err;
    if (s_ready) {
        return;
    }
    before = (unsigned)esp_get_free_heap_size();
    err = mdns_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "mdns init failed: %s free_heap=%u", esp_err_to_name(err),
                 (unsigned)esp_get_free_heap_size());
        return;
    }
    s_ready = 1;
    log_heap("mdns ready", before);
#endif
}

int lan_discover_load(char *dst, size_t dst_len) {
#if !CONFIG_RUSTFOX_MDNS_DISCOVERY
    (void)dst_len;
    if (dst) {
        dst[0] = '\0';
    }
    return 0;
#else
    nvs_handle_t handle;
    size_t need;
    esp_err_t err;
    if (!dst || dst_len < 8) {
        return 0;
    }
    dst[0] = '\0';
    if (nvs_open(NVS_NS, NVS_READONLY, &handle) != ESP_OK) {
        return 0;
    }
    need = dst_len;
    err = nvs_get_str(handle, NVS_KEY, dst, &need);
    nvs_close(handle);
    if (err != ESP_OK || !rf_discover_cached_uri_ok(dst)) {
        dst[0] = '\0';
        return 0;
    }
    return 1;
#endif
}

int lan_discover_store(const char *uri) {
#if !CONFIG_RUSTFOX_MDNS_DISCOVERY
    (void)uri;
    return -1;
#else
    nvs_handle_t handle;
    esp_err_t err;
    if (!rf_discover_cached_uri_ok(uri)) {
        return -1;
    }
    if (nvs_open(NVS_NS, NVS_READWRITE, &handle) != ESP_OK) {
        return -1;
    }
    err = nvs_set_str(handle, NVS_KEY, uri);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs store failed: %s", esp_err_to_name(err));
        return -1;
    }
    ESP_LOGI(TAG, "cached %s", uri);
    return 0;
#endif
}

int lan_discover_browse(char *dst, size_t dst_len) {
#if !CONFIG_RUSTFOX_MDNS_DISCOVERY
    (void)dst_len;
    if (dst) {
        dst[0] = '\0';
    }
    return -1;
#else
    /* One browse at a time, from the link task. Kept off the stack so the
     * link task does not grow by this much. */
    static char names[BROWSE_MAX][96];
    static char ipstore[BROWSE_MAX][ADDR_MAX][16];
    static const char *ipptr[BROWSE_MAX][ADDR_MAX];
    static char paths[BROWSE_MAX][48];
    static char protos[BROWSE_MAX][8];
    static rf_discover_service_t items[BROWSE_MAX];
    mdns_result_t *results = NULL;
    esp_err_t err;
    unsigned before;
    size_t count = 0;
    int index;
    if (!dst || dst_len == 0) {
        return -1;
    }
    dst[0] = '\0';
    lan_discover_prepare();
    before = (unsigned)esp_get_free_heap_size();
    err = mdns_query_ptr(RF_DISCOVER_SERVICE, RF_DISCOVER_PROTO, (uint32_t)CONFIG_RUSTFOX_MDNS_TIMEOUT_MS,
                         BROWSE_MAX, &results);
    if (err != ESP_OK || !results) {
        if (results) {
            mdns_query_results_free(results);
        }
        ESP_LOGI(TAG, "browse missed (%s) free_heap %u -> %u min_free=%u",
                 err == ESP_OK ? "none" : esp_err_to_name(err), before, (unsigned)esp_get_free_heap_size(),
                 (unsigned)esp_get_minimum_free_heap_size());
        return -1;
    }
    for (mdns_result_t *row = results; row && count < BROWSE_MAX; row = row->next) {
        rf_discover_kv_t kv[TXT_MAX];
        char keys[TXT_MAX][16];
        char vals[TXT_MAX][48];
        char audio[48];
        size_t nkv = 0;
        size_t naddr = 0;
        mdns_ip_addr_t *addr = row->addr;
        copy_cap(names[count], sizeof(names[count]), row->instance_name);
        while (addr && naddr < ADDR_MAX) {
            if (addr->addr.type == ESP_IPADDR_TYPE_V4) {
                snprintf(ipstore[count][naddr], sizeof(ipstore[count][naddr]), IPSTR,
                         IP2STR(&addr->addr.u_addr.ip4));
                ipptr[count][naddr] = ipstore[count][naddr];
                naddr++;
            }
            addr = addr->next;
        }
        if (row->txt) {
            size_t limit = row->txt_count;
            if (limit > 16) {
                limit = 16;
            }
            for (size_t t = 0; t < limit && nkv < TXT_MAX; t++) {
                const char *key = row->txt[t].key;
                if (!eq_key(key, "path") && !eq_key(key, "proto") && !eq_key(key, "audio")) {
                    continue;
                }
                copy_cap(keys[nkv], sizeof(keys[nkv]), key);
                txt_value(row, t, vals[nkv], sizeof(vals[nkv]));
                kv[nkv].key = keys[nkv];
                kv[nkv].value = vals[nkv];
                nkv++;
            }
        }
        if (rf_discover_apply_txt(kv, nkv, paths[count], sizeof(paths[count]), protos[count],
                                  sizeof(protos[count]), audio, sizeof(audio)) != 0) {
            continue;
        }
        items[count].instance = names[count];
        items[count].ipv4 = &ipptr[count][0];
        items[count].ipv4_count = naddr;
        items[count].port = (int)row->port;
        items[count].path = paths[count];
        items[count].proto_txt = protos[count];
        count++;
    }
    index = rf_discover_select(items, count, CONFIG_RUSTFOX_MDNS_INSTANCE, dst, dst_len);
    mdns_query_results_free(results);
    log_heap("browse", before);
    if (index < 0) {
        dst[0] = '\0';
        return -1;
    }
    return 0;
#endif
}
