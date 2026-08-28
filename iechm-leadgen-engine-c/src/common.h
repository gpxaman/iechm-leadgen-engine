/* Shared sizes, small helpers. Fixed-capacity buffers everywhere on purpose:
 * this is a from-scratch C port with no compiler available in the authoring
 * environment to catch use-after-free/realloc bugs, so the design trades a
 * bounded amount of memory for eliminating a whole class of unverifiable
 * pointer bugs. Growable arrays are used only for the record stores (see
 * store.h), where an append-only vector is simple enough to get right by
 * inspection.
 */
#ifndef IECHM_COMMON_H
#define IECHM_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define ID_LEN        40   /* "prefix_" + 16 hex chars, generous */
#define SHORT_LEN     80   /* platform, role, model_signature, event_type... */
#define TITLE_LEN     256
#define URL_LEN       256
#define TEXT_LEN      4096 /* raw_text, sanitized_text, proposal_text */
#define JSONTEXT_LEN  2048 /* rendered JSON-array text for list fields */
#define LIST_ITEM_LEN 48
#define LIST_MAX      12
#define NOTE_LEN      160
#define NOTES_MAX     8
#define STRATEGY_ID_LEN 128
#define ROLE_LEN      160

/* Copy src into a fixed dst[cap] buffer, always NUL-terminating, never
 * overflowing. Silent truncation beyond cap-1 is acceptable here: every
 * caller site uses generous buffer sizes for simulated/demo text. */
static inline void xcpy(char *dst, size_t cap, const char *src) {
    if (cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static inline double clampd(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

#endif
