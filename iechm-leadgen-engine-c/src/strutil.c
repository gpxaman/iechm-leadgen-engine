#include "strutil.h"
#include <ctype.h>

static unsigned char lc(unsigned char c) { return (unsigned char) tolower(c); }

int ci_find(const char *haystack, const char *needle, int from) {
    if (!haystack || !needle || !*needle) return -1;
    int hn = (int) strlen(haystack);
    int nn = (int) strlen(needle);
    if (from < 0) from = 0;
    for (int i = from; i + nn <= hn; i++) {
        int j = 0;
        for (; j < nn; j++) {
            if (lc((unsigned char) haystack[i + j]) != lc((unsigned char) needle[j])) break;
        }
        if (j == nn) return i;
    }
    return -1;
}

static bool boundary_before(const char *haystack, int pos) {
    if (pos <= 0) return true;
    return !is_word_char((unsigned char) haystack[pos - 1]);
}

static bool boundary_after(const char *haystack, int pos_after) {
    unsigned char c = (unsigned char) haystack[pos_after];
    if (c == '\0') return true;
    return !is_word_char(c);
}

bool has_word(const char *haystack, const char *needle) {
    int nn = (int) strlen(needle);
    int from = 0, pos;
    while ((pos = ci_find(haystack, needle, from)) >= 0) {
        if (boundary_before(haystack, pos) && boundary_after(haystack, pos + nn)) return true;
        from = pos + 1;
    }
    return false;
}

bool has_prefix_word(const char *haystack, const char *needle) {
    int from = 0, pos;
    while ((pos = ci_find(haystack, needle, from)) >= 0) {
        if (boundary_before(haystack, pos)) return true;
        from = pos + 1;
    }
    return false;
}

bool has_suffix_word(const char *haystack, const char *needle) {
    int nn = (int) strlen(needle);
    int from = 0, pos;
    while ((pos = ci_find(haystack, needle, from)) >= 0) {
        if (boundary_after(haystack, pos + nn)) return true;
        from = pos + 1;
    }
    return false;
}

bool has_plain(const char *haystack, const char *needle) {
    return ci_find(haystack, needle, 0) >= 0;
}

void slice_copy(char *dst, size_t cap, const char *src, int start, int len) {
    if (len < 0) len = 0;
    if ((size_t) len >= cap) len = (int) cap - 1;
    memcpy(dst, src + start, (size_t) len);
    dst[len] = '\0';
}

void str_prefix(char *dst, size_t cap, const char *src, size_t maxlen) {
    size_t n = strlen(src);
    if (n > maxlen) n = maxlen;
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}
