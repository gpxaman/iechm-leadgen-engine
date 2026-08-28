/* Hand-written case-insensitive text matching primitives standing in for
 * Python's `re` module. Deliberately NOT built on POSIX <regex.h>: this
 * environment has no compiler to verify whether glibc's GNU regex escapes
 * (\b, \w, \d) behave as expected here, and getting that wrong silently
 * would be worse than not using regex at all. Every pattern in the
 * original core/filters.py, core/security.py, core/classify.py is, on
 * inspection, either a plain case-insensitive substring or a substring with
 * a `\b` word-boundary requirement on one or both ends -- exactly what
 * these primitives cover, so each callsite is a direct, checkable
 * translation of the original regex rather than a re-implementation of
 * regex semantics in general.
 */
#ifndef IECHM_STRUTIL_H
#define IECHM_STRUTIL_H

#include "common.h"

static inline bool is_word_char(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/* Case-insensitive search for `needle` in `haystack` starting at byte index
 * `from`. Returns the index of the first match, or -1. */
int ci_find(const char *haystack, const char *needle, int from);

/* `\bneedle\b` -- word boundary required on both sides of the match. */
bool has_word(const char *haystack, const char *needle);
/* `\bneedle` -- boundary required only immediately before the match. */
bool has_prefix_word(const char *haystack, const char *needle);
/* `needle\b` -- boundary required only immediately after the match. */
bool has_suffix_word(const char *haystack, const char *needle);
/* `needle` -- plain substring, no boundary requirement either side. */
bool has_plain(const char *haystack, const char *needle);

/* Copies a `len`-byte slice of `src` starting at `start` into dst[cap]. */
void slice_copy(char *dst, size_t cap, const char *src, int start, int len);

/* Python's `src[:maxlen]` -- first maxlen chars, or fewer if src is shorter. */
void str_prefix(char *dst, size_t cap, const char *src, size_t maxlen);

#endif
