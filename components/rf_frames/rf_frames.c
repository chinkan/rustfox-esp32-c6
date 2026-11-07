// SPDX-License-Identifier: MIT
// Copyright (c) 2026 chinkan.ai

#include "rf_frames.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int is_ws(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static size_t utf8_codepoints_n(const char *s, size_t n) {
    size_t count = 0;
    for (size_t i = 0; i < n; i++) {
        if (((unsigned char)s[i] & 0xC0) != 0x80) {
            count++;
        }
    }
    return count;
}

typedef struct {
    char *dst;
    size_t cap;
    size_t used;
    int err;
} wr_t;

static void wr_bytes(wr_t *w, const void *src, size_t n) {
    if (w->err) {
        return;
    }
    if (n >= w->cap - w->used) {
        w->err = 1;
        return;
    }
    memcpy(w->dst + w->used, src, n);
    w->used += n;
}

static void wr_lit(wr_t *w, const char *lit) {
    wr_bytes(w, lit, strlen(lit));
}

static void wr_json_str(wr_t *w, const char *s, size_t n) {
    wr_lit(w, "\"");
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        char esc[8];
        if (c == '"' || c == '\\') {
            esc[0] = '\\';
            esc[1] = (char)c;
            wr_bytes(w, esc, 2);
        } else if (c < 0x20) {
            int len = snprintf(esc, sizeof(esc), "\\u%04x", c);
            wr_bytes(w, esc, (size_t)len);
        } else {
            wr_bytes(w, &c, 1);
        }
    }
    wr_lit(w, "\"");
}

static int wr_finish(wr_t *w) {
    if (w->err || w->used >= w->cap) {
        return RF_ERR_NOSPACE;
    }
    w->dst[w->used] = '\0';
    return (int)w->used;
}

int rf_encode_ping(char *dst, size_t dst_len) {
    if (!dst || dst_len == 0) {
        return RF_ERR_ARG;
    }
    wr_t w = {dst, dst_len, 0, 0};
    wr_lit(&w, "{\"type\":\"ping\"}");
    return wr_finish(&w);
}

int rf_encode_auth(char *dst, size_t dst_len, const char *token) {
    if (!dst || dst_len == 0 || !token) {
        return RF_ERR_ARG;
    }
    while (*token && is_ws(*token)) {
        token++;
    }
    size_t n = strlen(token);
    while (n && is_ws(token[n - 1])) {
        n--;
    }
    if (n == 0) {
        return RF_ERR_ARG;
    }
    wr_t w = {dst, dst_len, 0, 0};
    wr_lit(&w, "{\"type\":\"auth\",\"token\":");
    wr_json_str(&w, token, n);
    wr_lit(&w, "}");
    return wr_finish(&w);
}

int rf_encode_user_text(char *dst, size_t dst_len, const char *text) {
    if (!dst || dst_len == 0 || !text) {
        return RF_ERR_ARG;
    }
    while (*text && isspace((unsigned char)*text)) {
        text++;
    }
    size_t n = strlen(text);
    while (n && isspace((unsigned char)text[n - 1])) {
        n--;
    }
    if (n == 0) {
        return RF_ERR_BAD_FRAME;
    }
    if (utf8_codepoints_n(text, n) > RF_MAX_USER_TEXT_CHARS) {
        return RF_ERR_RANGE;
    }
    wr_t w = {dst, dst_len, 0, 0};
    wr_lit(&w, "{\"type\":\"user_text\",\"text\":");
    wr_json_str(&w, text, n);
    wr_lit(&w, "}");
    return wr_finish(&w);
}

/* Same rules as RustFox sanitize_meta: trim, drop controls and [] , cap 64. */
static int sanitize_meta(const char *in, char *out, size_t out_cap) {
    if (!out || out_cap == 0) {
        return -1;
    }
    out[0] = '\0';
    if (!in) {
        return 0;
    }
    while (*in && isspace((unsigned char)*in)) {
        in++;
    }
    size_t o = 0;
    int points = 0;
    while (*in && points < RF_META_MAX_CHARS) {
        unsigned char c = (unsigned char)*in;
        if (c < 0x20 || c == 0x7F || c == '[' || c == ']') {
            in++;
            continue;
        }
        int len = 1;
        if ((c & 0x80) == 0) {
            len = 1;
        } else if ((c & 0xE0) == 0xC0) {
            len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
        } else {
            in++;
            continue;
        }
        int ok = 1;
        for (int i = 1; i < len; i++) {
            if (((unsigned char)in[i] & 0xC0) != 0x80) {
                ok = 0;
                break;
            }
        }
        if (!ok) {
            in++;
            continue;
        }
        if (o + (size_t)len >= out_cap) {
            break;
        }
        memcpy(out + o, in, (size_t)len);
        o += (size_t)len;
        in += len;
        points++;
    }
    while (o > 0 && isspace((unsigned char)out[o - 1])) {
        o--;
    }
    out[o] = '\0';
    return (int)o;
}

int rf_encode_device_hello(char *dst, size_t dst_len, const char *device_id,
                           const char *room, const char *fw_version) {
    if (!dst || dst_len == 0) {
        return RF_ERR_ARG;
    }
    char id[RF_META_MAX_CHARS * 4 + 1];
    char rm[RF_META_MAX_CHARS * 4 + 1];
    char fw[RF_META_MAX_CHARS * 4 + 1];
    if (sanitize_meta(device_id, id, sizeof(id)) <= 0) {
        return RF_ERR_ARG;
    }
    sanitize_meta(room, rm, sizeof(rm));
    sanitize_meta(fw_version, fw, sizeof(fw));

    wr_t w = {dst, dst_len, 0, 0};
    wr_lit(&w, "{\"type\":\"device_hello\",\"device_id\":");
    wr_json_str(&w, id, strlen(id));
    if (rm[0]) {
        wr_lit(&w, ",\"room\":");
        wr_json_str(&w, rm, strlen(rm));
    }
    if (fw[0]) {
        wr_lit(&w, ",\"fw_version\":");
        wr_json_str(&w, fw, strlen(fw));
    }
    wr_lit(&w, "}");
    return wr_finish(&w);
}

const char *rf_frame_type_name(rf_frame_type_t type) {
    switch (type) {
    case RF_TYPE_AUTH:
        return "auth";
    case RF_TYPE_USER_TEXT:
        return "user_text";
    case RF_TYPE_PING:
        return "ping";
    case RF_TYPE_DEVICE_HELLO:
        return "device_hello";
    case RF_TYPE_ASSISTANT_TEXT:
        return "assistant_text";
    case RF_TYPE_DEVICE_ACK:
        return "device_ack";
    case RF_TYPE_PONG:
        return "pong";
    case RF_TYPE_ERROR:
        return "error";
    case RF_TYPE_UNKNOWN:
        return "unknown";
    case RF_TYPE_NONE:
    default:
        return "none";
    }
}

static rf_frame_type_t type_from_name(const char *name) {
    if (strcmp(name, "auth") == 0) {
        return RF_TYPE_AUTH;
    }
    if (strcmp(name, "user_text") == 0) {
        return RF_TYPE_USER_TEXT;
    }
    if (strcmp(name, "ping") == 0) {
        return RF_TYPE_PING;
    }
    if (strcmp(name, "device_hello") == 0) {
        return RF_TYPE_DEVICE_HELLO;
    }
    if (strcmp(name, "assistant_text") == 0) {
        return RF_TYPE_ASSISTANT_TEXT;
    }
    if (strcmp(name, "device_ack") == 0) {
        return RF_TYPE_DEVICE_ACK;
    }
    if (strcmp(name, "pong") == 0) {
        return RF_TYPE_PONG;
    }
    if (strcmp(name, "error") == 0) {
        return RF_TYPE_ERROR;
    }
    return RF_TYPE_UNKNOWN;
}

static void skip_ws(const char *s, size_t n, size_t *i) {
    while (*i < n && is_ws(s[*i])) {
        (*i)++;
    }
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static int append_byte(char *dst, size_t cap, size_t *o, int *trunc, unsigned char c) {
    if (*o + 1 < cap) {
        dst[(*o)++] = (char)c;
        return 0;
    }
    *trunc = 1;
    return 0;
}

static int read_string(const char *s, size_t n, size_t *io, char *dst, size_t cap, int *trunc) {
    size_t i = *io;
    if (i >= n || s[i] != '"') {
        return -1;
    }
    i++;
    size_t o = 0;
    int truncated = 0;
    if (cap == 0) {
        return -1;
    }
    while (i < n) {
        unsigned char c = (unsigned char)s[i++];
        if (c == '"') {
            dst[o] = '\0';
            *io = i;
            if (trunc) {
                *trunc = truncated;
            }
            return 0;
        }
        if (c == '\\') {
            if (i >= n) {
                return -1;
            }
            char e = s[i++];
            switch (e) {
            case '"':
            case '\\':
            case '/':
                c = (unsigned char)e;
                break;
            case 'b':
                c = '\b';
                break;
            case 'f':
                c = '\f';
                break;
            case 'n':
                c = '\n';
                break;
            case 'r':
                c = '\r';
                break;
            case 't':
                c = '\t';
                break;
            case 'u': {
                if (i + 4 > n) {
                    return -1;
                }
                unsigned cp = 0;
                for (int h = 0; h < 4; h++) {
                    int nib = hex_nibble(s[i++]);
                    if (nib < 0) {
                        return -1;
                    }
                    cp = (cp << 4) | (unsigned)nib;
                }
                char tmp[3];
                int tn = 0;
                if (cp <= 0x7F) {
                    tmp[0] = (char)cp;
                    tn = 1;
                } else if (cp <= 0x7FF) {
                    tmp[0] = (char)(0xC0 | (cp >> 6));
                    tmp[1] = (char)(0x80 | (cp & 0x3F));
                    tn = 2;
                } else {
                    tmp[0] = (char)(0xE0 | (cp >> 12));
                    tmp[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    tmp[2] = (char)(0x80 | (cp & 0x3F));
                    tn = 3;
                }
                for (int t = 0; t < tn; t++) {
                    append_byte(dst, cap, &o, &truncated, (unsigned char)tmp[t]);
                }
                continue;
            }
            default:
                return -1;
            }
        }
        append_byte(dst, cap, &o, &truncated, c);
    }
    return -1;
}

static int skip_string(const char *s, size_t n, size_t *io) {
    size_t i = *io;
    if (i >= n || s[i] != '"') {
        return -1;
    }
    i++;
    while (i < n) {
        char c = s[i++];
        if (c == '"') {
            *io = i;
            return 0;
        }
        if (c == '\\') {
            if (i >= n) {
                return -1;
            }
            char e = s[i++];
            if (e == 'u') {
                if (i + 4 > n) {
                    return -1;
                }
                for (int h = 0; h < 4; h++) {
                    if (hex_nibble(s[i++]) < 0) {
                        return -1;
                    }
                }
            }
        }
    }
    return -1;
}

static int skip_value(const char *s, size_t n, size_t *io) {
    skip_ws(s, n, io);
    if (*io >= n) {
        return -1;
    }
    char c = s[*io];
    if (c == '"') {
        return skip_string(s, n, io);
    }
    if (c == '{' || c == '[') {
        char open = c;
        char close = (c == '{') ? '}' : ']';
        int depth = 0;
        while (*io < n) {
            char ch = s[*io];
            if (ch == '"') {
                if (skip_string(s, n, io) != 0) {
                    return -1;
                }
                continue;
            }
            (*io)++;
            if (ch == open) {
                depth++;
            } else if (ch == close) {
                depth--;
                if (depth == 0) {
                    return 0;
                }
            }
        }
        return -1;
    }
    if (c == 't' || c == 'f' || c == 'n' || c == '-' || (c >= '0' && c <= '9')) {
        while (*io < n) {
            char ch = s[*io];
            if (ch == ',' || ch == '}' || ch == ']' || is_ws(ch)) {
                break;
            }
            (*io)++;
        }
        return 0;
    }
    return -1;
}

static void copy_bounded(char *dst, size_t cap, const char *value) {
    size_t i = 0;
    if (cap == 0) {
        return;
    }
    while (value[i] && i + 1 < cap) {
        dst[i] = value[i];
        i++;
    }
    dst[i] = '\0';
}

static void copy_field(const char *key, const char *value, int truncated, rf_frame_t *out) {
    if (strcmp(key, "type") == 0) {
        copy_bounded(out->type_name, sizeof(out->type_name), value);
    } else if (strcmp(key, "token") == 0) {
        copy_bounded(out->token, sizeof(out->token), value);
    } else if (strcmp(key, "text") == 0) {
        copy_bounded(out->text, sizeof(out->text), value);
        out->text_truncated = truncated;
    } else if (strcmp(key, "code") == 0) {
        copy_bounded(out->code, sizeof(out->code), value);
    } else if (strcmp(key, "message") == 0) {
        copy_bounded(out->message, sizeof(out->message), value);
    } else if (strcmp(key, "status") == 0) {
        copy_bounded(out->status, sizeof(out->status), value);
    } else if (strcmp(key, "device_id") == 0) {
        copy_bounded(out->device_id, sizeof(out->device_id), value);
    } else if (strcmp(key, "room") == 0) {
        copy_bounded(out->room, sizeof(out->room), value);
    } else if (strcmp(key, "fw_version") == 0) {
        copy_bounded(out->fw_version, sizeof(out->fw_version), value);
    }
}

int rf_parse_frame(const char *json, size_t len, rf_frame_t *out) {
    if (!json || !out) {
        return RF_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    size_t i = 0;
    skip_ws(json, len, &i);
    if (i >= len || json[i] != '{') {
        return RF_ERR_BAD_FRAME;
    }
    i++;
    skip_ws(json, len, &i);
    if (i < len && json[i] == '}') {
        return RF_ERR_BAD_FRAME;
    }
    while (i < len) {
        skip_ws(json, len, &i);
        char key[32];
        int key_trunc = 0;
        if (read_string(json, len, &i, key, sizeof(key), &key_trunc) != 0 || key_trunc) {
            return RF_ERR_BAD_FRAME;
        }
        skip_ws(json, len, &i);
        if (i >= len || json[i] != ':') {
            return RF_ERR_BAD_FRAME;
        }
        i++;
        skip_ws(json, len, &i);
        if (i < len && json[i] == '"') {
            char value[RF_FRAME_TEXT_CAP];
            int trunc = 0;
            if (read_string(json, len, &i, value, sizeof(value), &trunc) != 0) {
                return RF_ERR_BAD_FRAME;
            }
            copy_field(key, value, trunc, out);
        } else if (skip_value(json, len, &i) != 0) {
            return RF_ERR_BAD_FRAME;
        }
        skip_ws(json, len, &i);
        if (i < len && json[i] == ',') {
            i++;
            continue;
        }
        if (i < len && json[i] == '}') {
            i++;
            skip_ws(json, len, &i);
            if (i != len) {
                return RF_ERR_BAD_FRAME;
            }
            if (out->type_name[0] == '\0') {
                return RF_ERR_BAD_FRAME;
            }
            out->type = type_from_name(out->type_name);
            return RF_OK;
        }
        return RF_ERR_BAD_FRAME;
    }
    return RF_ERR_BAD_FRAME;
}
