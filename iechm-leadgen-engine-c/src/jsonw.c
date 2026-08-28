#include "jsonw.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void jw_reserve(Jsonw *w, size_t extra) {
    if (w->len + extra + 1 <= w->cap) return;
    size_t ncap = w->cap ? w->cap * 2 : 256;
    while (ncap < w->len + extra + 1) ncap *= 2;
    char *nb = realloc(w->buf, ncap);
    if (!nb) { fprintf(stderr, "jsonw: out of memory\n"); exit(1); }
    w->buf = nb;
    w->cap = ncap;
}

static void jw_putc(Jsonw *w, char c) {
    jw_reserve(w, 1);
    w->buf[w->len++] = c;
    w->buf[w->len] = '\0';
}

static void jw_puts(Jsonw *w, const char *s) {
    size_t n = strlen(s);
    jw_reserve(w, n);
    memcpy(w->buf + w->len, s, n);
    w->len += n;
    w->buf[w->len] = '\0';
}

void jw_init(Jsonw *w) {
    w->buf = NULL;
    w->len = 0;
    w->cap = 0;
    w->depth = 0;
    jw_reserve(w, 1);
    w->buf[0] = '\0';
}

char *jw_finish(Jsonw *w) {
    char *out = w->buf;
    w->buf = NULL;
    w->cap = 0;
    w->len = 0;
    return out ? out : strdup("");
}

/* Called before writing any element/key at the current depth: emits a comma
 * if this isn't the first element, then clears the "first" flag. */
static void jw_before_element(Jsonw *w) {
    if (w->depth <= 0) return;
    int d = w->depth - 1;
    if (!w->first[d]) jw_putc(w, ',');
    w->first[d] = false;
}

static void jw_push(Jsonw *w) {
    if (w->depth >= JW_MAX_DEPTH) { fprintf(stderr, "jsonw: max depth exceeded\n"); exit(1); }
    w->first[w->depth] = true;
    w->depth++;
}

static void jw_pop(Jsonw *w) {
    if (w->depth > 0) w->depth--;
}

void jw_obj_open(Jsonw *w) { jw_before_element(w); jw_putc(w, '{'); jw_push(w); }
void jw_obj_close(Jsonw *w) { jw_pop(w); jw_putc(w, '}'); }
void jw_arr_open(Jsonw *w) { jw_before_element(w); jw_putc(w, '['); jw_push(w); }
void jw_arr_close(Jsonw *w) { jw_pop(w); jw_putc(w, ']'); }

static void jw_escaped_str(Jsonw *w, const char *s) {
    jw_putc(w, '"');
    for (const unsigned char *p = (const unsigned char *) s; *p; p++) {
        switch (*p) {
            case '"': jw_puts(w, "\\\""); break;
            case '\\': jw_puts(w, "\\\\"); break;
            case '\n': jw_puts(w, "\\n"); break;
            case '\r': jw_puts(w, "\\r"); break;
            case '\t': jw_puts(w, "\\t"); break;
            default:
                if (*p < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof buf, "\\u%04x", *p);
                    jw_puts(w, buf);
                } else {
                    jw_putc(w, (char) *p);
                }
        }
    }
    jw_putc(w, '"');
}

void jw_key(Jsonw *w, const char *key) {
    jw_before_element(w);
    jw_escaped_str(w, key);
    jw_putc(w, ':');
    /* the value that follows must NOT re-trigger a comma/first-check, so we
     * pretend we're still "first" at this depth until the value is written */
    if (w->depth > 0) w->first[w->depth - 1] = true;
}

void jw_str(Jsonw *w, const char *value) {
    if (!value) { jw_null(w); return; }
    jw_before_element(w);
    jw_escaped_str(w, value);
}

void jw_str_or_empty(Jsonw *w, const char *value) {
    jw_before_element(w);
    jw_escaped_str(w, value ? value : "");
}

void jw_raw_json(Jsonw *w, const char *already_valid_json) {
    jw_before_element(w);
    jw_puts(w, already_valid_json && *already_valid_json ? already_valid_json : "null");
}

void jw_int(Jsonw *w, long long value) {
    jw_before_element(w);
    char buf[32];
    snprintf(buf, sizeof buf, "%lld", value);
    jw_puts(w, buf);
}

void jw_int_or_null(Jsonw *w, long long value, bool has_value) {
    if (!has_value) { jw_null(w); return; }
    jw_int(w, value);
}

void jw_dbl(Jsonw *w, double value, int decimals) {
    jw_before_element(w);
    char buf[64];
    snprintf(buf, sizeof buf, "%.*f", decimals, value);
    jw_puts(w, buf);
}

void jw_dbl_or_null(Jsonw *w, double value, int decimals, bool has_value) {
    if (!has_value) { jw_null(w); return; }
    jw_dbl(w, value, decimals);
}

void jw_bool(Jsonw *w, bool value) {
    jw_before_element(w);
    jw_puts(w, value ? "true" : "false");
}

void jw_null(Jsonw *w) {
    jw_before_element(w);
    jw_puts(w, "null");
}

void jw_kv_str(Jsonw *w, const char *key, const char *value) { jw_key(w, key); jw_str(w, value); }
void jw_kv_str_or_empty(Jsonw *w, const char *key, const char *value) { jw_key(w, key); jw_str_or_empty(w, value); }
void jw_kv_raw(Jsonw *w, const char *key, const char *already_valid_json) { jw_key(w, key); jw_raw_json(w, already_valid_json); }
void jw_kv_int(Jsonw *w, const char *key, long long value) { jw_key(w, key); jw_int(w, value); }
void jw_kv_int_or_null(Jsonw *w, const char *key, long long value, bool has_value) { jw_key(w, key); jw_int_or_null(w, value, has_value); }
void jw_kv_dbl(Jsonw *w, const char *key, double value, int decimals) { jw_key(w, key); jw_dbl(w, value, decimals); }
void jw_kv_dbl_or_null(Jsonw *w, const char *key, double value, int decimals, bool has_value) { jw_key(w, key); jw_dbl_or_null(w, value, decimals, has_value); }
void jw_kv_bool(Jsonw *w, const char *key, bool value) { jw_key(w, key); jw_bool(w, value); }
