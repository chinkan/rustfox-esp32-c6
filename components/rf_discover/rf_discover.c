// SPDX-License-Identifier: MIT
// Copyright (c) 2026 chinkan.ai

#include "rf_discover.h"

#include <stdio.h>
#include <string.h>

static char ascii_lower(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static int eq_ci(const char *a, const char *b) {
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        if (ascii_lower(*a) != ascii_lower(*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int parse_ipv4(const char *s, unsigned char out[4]) {
    if (!s || !out) {
        return -1;
    }
    const char *p = s;
    for (int part = 0; part < 4; part++) {
        if (*p < '0' || *p > '9') {
            return -1;
        }
        int value = 0;
        int digits = 0;
        while (*p >= '0' && *p <= '9') {
            value = value * 10 + (*p - '0');
            if (value > 255) {
                return -1;
            }
            digits++;
            if (digits > 3) {
                return -1;
            }
            p++;
        }
        if (digits == 0) {
            return -1;
        }
        out[part] = (unsigned char)value;
        if (part < 3) {
            if (*p != '.') {
                return -1;
            }
            p++;
        }
    }
    if (*p != '\0') {
        return -1;
    }
    return 0;
}

int rf_discover_ipv4_ok(const char *dotted) {
    unsigned char o[4];
    if (parse_ipv4(dotted, o) != 0) {
        return 0;
    }
    /* 0/8 is unspecified. 127/8 is loopback. 172/8 is WSL, Hyper-V, and
     * Docker NAT ("172.x"). 169.254/16 is APIPA. 100/8 is Tailscale
     * ("100.x"). 224/4 and above are multicast or reserved. */
    if (o[0] == 0 || o[0] == 127 || o[0] == 100 || o[0] == 172) {
        return 0;
    }
    if (o[0] == 169 && o[1] == 254) {
        return 0;
    }
    if (o[0] >= 224) {
        return 0;
    }
    return 1;
}

int rf_discover_build_uri(const char *ipv4, int port, const char *path, char *dst, size_t dst_len) {
    unsigned char o[4];
    if (!dst || dst_len == 0) {
        return -1;
    }
    dst[0] = '\0';
    if (parse_ipv4(ipv4, o) != 0 || !rf_discover_ipv4_ok(ipv4)) {
        return -1;
    }
    if (port < 1 || port > 65535) {
        return -1;
    }
    if (!path || strcmp(path, RF_DISCOVER_PATH) != 0) {
        return -1;
    }
    int n = snprintf(dst, dst_len, "ws://%u.%u.%u.%u:%d%s", o[0], o[1], o[2], o[3], port, path);
    if (n < 0 || (size_t)n >= dst_len) {
        dst[0] = '\0';
        return -1;
    }
    return 0;
}

int rf_discover_cached_uri_ok(const char *uri) {
    unsigned char o[4];
    char ip[16];
    size_t i = 0;
    if (!uri || strncmp(uri, "ws://", 5) != 0) {
        return 0;
    }
    const char *p = uri + 5;
    while (p[i] && p[i] != ':' && i + 1 < sizeof(ip)) {
        ip[i] = p[i];
        i++;
    }
    if (p[i] != ':') {
        return 0;
    }
    ip[i] = '\0';
    if (parse_ipv4(ip, o) != 0 || !rf_discover_ipv4_ok(ip)) {
        return 0;
    }
    p += i + 1;
    if (*p < '0' || *p > '9') {
        return 0;
    }
    int port = 0;
    int digits = 0;
    while (*p >= '0' && *p <= '9') {
        port = port * 10 + (*p - '0');
        if (port > 65535) {
            return 0;
        }
        digits++;
        if (digits > 5) {
            return 0;
        }
        p++;
    }
    if (digits == 0 || port < 1) {
        return 0;
    }
    return strcmp(p, RF_DISCOVER_PATH) == 0;
}

static int copy_exact(char *dst, size_t dst_len, const char *src) {
    size_t n = strlen(src);
    if (n + 1 > dst_len) {
        return -1;
    }
    memcpy(dst, src, n + 1);
    return 0;
}

int rf_discover_apply_txt(const rf_discover_kv_t *kv, size_t count, char *path, size_t path_len,
                          char *proto, size_t proto_len, char *audio, size_t audio_len) {
    int saw_path = 0;
    int saw_proto = 0;
    if (!path || path_len == 0 || !proto || proto_len == 0 || !audio || audio_len == 0) {
        return -1;
    }
    path[0] = '\0';
    proto[0] = '\0';
    audio[0] = '\0';
    for (size_t i = 0; i < count; i++) {
        const char *key;
        const char *value;
        if (!kv) {
            break;
        }
        key = kv[i].key;
        value = kv[i].value;
        if (!key || !value) {
            continue;
        }
        if (!saw_path && eq_ci(key, "path") && strcmp(value, RF_DISCOVER_PATH) == 0) {
            if (copy_exact(path, path_len, RF_DISCOVER_PATH) != 0) {
                return -1;
            }
            saw_path = 1;
        } else if (!saw_proto && eq_ci(key, "proto") && strcmp(value, RF_DISCOVER_PROTO_TXT) == 0) {
            if (copy_exact(proto, proto_len, RF_DISCOVER_PROTO_TXT) != 0) {
                return -1;
            }
            saw_proto = 1;
        } else if (audio[0] == '\0' && eq_ci(key, "audio") && strcmp(value, RF_DISCOVER_AUDIO_PATH) == 0) {
            if (copy_exact(audio, audio_len, RF_DISCOVER_AUDIO_PATH) != 0) {
                return -1;
            }
        }
    }
    if (!saw_path || !saw_proto) {
        path[0] = '\0';
        proto[0] = '\0';
        audio[0] = '\0';
        return -1;
    }
    return 0;
}

static void instance_label(const char *instance, char *dst, size_t dst_len) {
    static const char mark[] = "._rustfox-device._tcp";
    size_t mlen = sizeof(mark) - 1;
    size_t n;
    const char *cut = NULL;
    if (!dst || dst_len == 0) {
        return;
    }
    dst[0] = '\0';
    if (!instance) {
        return;
    }
    for (size_t i = 0; instance[i]; i++) {
        size_t j = 0;
        while (j < mlen && instance[i + j] && ascii_lower(instance[i + j]) == mark[j]) {
            j++;
        }
        if (j == mlen) {
            cut = instance + i;
            break;
        }
    }
    n = cut ? (size_t)(cut - instance) : strlen(instance);
    if (n >= dst_len) {
        n = dst_len - 1;
    }
    memcpy(dst, instance, n);
    dst[n] = '\0';
}

static int service_usable(const rf_discover_service_t *svc, const char **chosen) {
    if (!svc || !svc->path || !svc->proto_txt || !svc->ipv4) {
        return 0;
    }
    if (strcmp(svc->path, RF_DISCOVER_PATH) != 0 || strcmp(svc->proto_txt, RF_DISCOVER_PROTO_TXT) != 0) {
        return 0;
    }
    if (svc->port < 1 || svc->port > 65535) {
        return 0;
    }
    for (size_t i = 0; i < svc->ipv4_count; i++) {
        if (rf_discover_ipv4_ok(svc->ipv4[i])) {
            if (chosen) {
                *chosen = svc->ipv4[i];
            }
            return 1;
        }
    }
    return 0;
}

static int label_is(const rf_discover_service_t *svc, const char *name) {
    char label[96];
    if (!name || !name[0]) {
        return 0;
    }
    instance_label(svc->instance, label, sizeof(label));
    return eq_ci(label, name);
}

static int pick_named(const rf_discover_service_t *items, size_t count, const char *name) {
    for (size_t i = 0; i < count; i++) {
        if (label_is(&items[i], name) && service_usable(&items[i], NULL)) {
            return (int)i;
        }
    }
    return -1;
}

int rf_discover_select(const rf_discover_service_t *items, size_t count, const char *configured_name,
                       char *uri, size_t uri_len) {
    int idx = -1;
    const char *ip = NULL;
    if (!uri || uri_len == 0) {
        return -1;
    }
    uri[0] = '\0';
    if (!items && count > 0) {
        return -1;
    }
    if (configured_name && configured_name[0] && !eq_ci(configured_name, RF_DISCOVER_INSTANCE)) {
        idx = pick_named(items, count, configured_name);
    }
    if (idx < 0) {
        idx = pick_named(items, count, RF_DISCOVER_INSTANCE);
    }
    if (idx < 0) {
        for (size_t i = 0; i < count; i++) {
            if (service_usable(&items[i], NULL)) {
                idx = (int)i;
                break;
            }
        }
    }
    if (idx < 0 || !service_usable(&items[idx], &ip)) {
        return -1;
    }
    if (rf_discover_build_uri(ip, items[idx].port, items[idx].path, uri, uri_len) != 0) {
        return -1;
    }
    return idx;
}

rf_disc_step_t rf_discover_next(int have_nvs, int nvs_failed, rf_browse_state_t browse) {
    if (have_nvs && !nvs_failed) {
        return RF_DISC_STEP_NVS;
    }
    if (browse == RF_BROWSE_NOT_TRIED) {
        return RF_DISC_STEP_BROWSE;
    }
    if (browse == RF_BROWSE_FOUND) {
        return RF_DISC_STEP_DISCOVERED;
    }
    return RF_DISC_STEP_COMPILED;
}

int rf_discover_cache_on_success(rf_disc_step_t step) {
    return step == RF_DISC_STEP_DISCOVERED;
}
