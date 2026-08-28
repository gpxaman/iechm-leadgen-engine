/* Minimal growable-buffer JSON writer. No parser needed anywhere except the
 * one POST body shape ({"day": N}), handled ad hoc in http.c -- writing this
 * as a hand-rolled encoder-only library (rather than a general JSON library)
 * matches the narrow shape of what this backend actually needs to produce.
 *
 * Usage: jw_init -> jw_obj_open/jw_arr_open, jw_key + value writers, matching
 * jw_obj_close/jw_arr_close, then jw_finish to take ownership of the buffer
 * (caller frees it). Comma placement is tracked automatically per nesting
 * level via a "first element in this container" stack.
 */
#ifndef IECHM_JSONW_H
#define IECHM_JSONW_H

#include <stdbool.h>
#include <stddef.h>

#define JW_MAX_DEPTH 16

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    bool first[JW_MAX_DEPTH]; /* true if no element written yet at this depth */
    int depth;
} Jsonw;

void jw_init(Jsonw *w);
char *jw_finish(Jsonw *w); /* returns malloc'd NUL-terminated string, caller frees */

void jw_obj_open(Jsonw *w);
void jw_obj_close(Jsonw *w);
void jw_arr_open(Jsonw *w);
void jw_arr_close(Jsonw *w);

void jw_key(Jsonw *w, const char *key); /* writes "key": (handles comma) */

/* Value writers -- each also handles the leading comma when used as a bare
 * array element (call after jw_arr_open / previous element, no jw_key). */
void jw_str(Jsonw *w, const char *value);       /* NULL -> null */
void jw_str_or_empty(Jsonw *w, const char *value); /* NULL/"" both -> "" */
void jw_raw_json(Jsonw *w, const char *already_valid_json); /* embedded as-is, e.g. pre-rendered arrays */
void jw_int(Jsonw *w, long long value);
void jw_int_or_null(Jsonw *w, long long value, bool has_value);
void jw_dbl(Jsonw *w, double value, int decimals);
void jw_dbl_or_null(Jsonw *w, double value, int decimals, bool has_value);
void jw_bool(Jsonw *w, bool value);
void jw_null(Jsonw *w);

/* Convenience: obj-scoped key+value in one call. */
void jw_kv_str(Jsonw *w, const char *key, const char *value);
void jw_kv_str_or_empty(Jsonw *w, const char *key, const char *value);
void jw_kv_raw(Jsonw *w, const char *key, const char *already_valid_json);
void jw_kv_int(Jsonw *w, const char *key, long long value);
void jw_kv_int_or_null(Jsonw *w, const char *key, long long value, bool has_value);
void jw_kv_dbl(Jsonw *w, const char *key, double value, int decimals);
void jw_kv_dbl_or_null(Jsonw *w, const char *key, double value, int decimals, bool has_value);
void jw_kv_bool(Jsonw *w, const char *key, bool value);

#endif
