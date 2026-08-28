/* core/security.py port. The original uses Python regex with capture
 * groups; here each pattern is expanded into its finite set of literal
 * phrase alternatives (all of `core/channels/_util.py`'s injected template
 * strings are themselves fixed literals, so this loses no coverage in
 * practice) plus a couple of small hand-written scanners for the two
 * patterns with real quantifiers (a digit run, and \s*). Every pattern
 * below is a direct, checkable translation of the corresponding line in
 * core/security.py.
 */
#include "security.h"
#include "strutil.h"
#include <ctype.h>
#include <string.h>

typedef int (*PatternFind)(const char *text, int from, int *out_len);

static int find_variant_min(const char *text, int from, const char **variants, int n, int *out_len) {
    int best_pos = -1, best_len = 0;
    for (int i = 0; i < n; i++) {
        int p = ci_find(text, variants[i], from);
        if (p >= 0 && (best_pos < 0 || p < best_pos)) { best_pos = p; best_len = (int) strlen(variants[i]); }
    }
    if (out_len) *out_len = best_len;
    return best_pos;
}

/* ------------------------------------------------------------- anti-bot */

static bool try_anchors_capture(const char *text, const char **anchors, int n_anchors,
                                 const char **fillers, char *out, size_t outcap) {
    int best_pos = -1;
    const char *best_anchor = NULL;
    for (int i = 0; i < n_anchors; i++) {
        int p = ci_find(text, anchors[i], 0);
        if (p >= 0 && (best_pos < 0 || p < best_pos)) { best_pos = p; best_anchor = anchors[i]; }
    }
    if (best_pos < 0) return false;

    int cursor = best_pos + (int) strlen(best_anchor);
    while (text[cursor] == ' ') cursor++;
    if (fillers) {
        for (int i = 0; fillers[i]; i++) {
            size_t flen = strlen(fillers[i]);
            if (ci_find(text, fillers[i], cursor) == cursor) { cursor += (int) flen; break; }
        }
    }
    if (text[cursor] == '\'' || text[cursor] == '"') cursor++;
    int start = cursor;
    while (is_word_char((unsigned char) text[cursor])) cursor++;
    int len = cursor - start;
    if (len <= 0) return false;
    slice_copy(out, outcap, text, start, len);
    return true;
}

/* ------------------------------------------------------------- math check */

static int find_math_expr(const char *text, int from, int *out_len) {
    int pos = ci_find(text, "what is", from);
    while (pos >= 0) {
        int cursor = pos + 7;
        while (text[cursor] == ' ') cursor++;
        int d1 = cursor;
        while (isdigit((unsigned char) text[cursor])) cursor++;
        if (cursor > d1) {
            while (text[cursor] == ' ') cursor++;
            char op = text[cursor];
            if (op == '+' || op == '-' || op == '*' || op == 'x' || op == 'X') {
                cursor++;
                while (text[cursor] == ' ') cursor++;
                int d2 = cursor;
                while (isdigit((unsigned char) text[cursor])) cursor++;
                if (cursor > d2) {
                    *out_len = cursor - pos;
                    return pos;
                }
            }
        }
        pos = ci_find(text, "what is", pos + 1);
    }
    return -1;
}

/* --------------------------------------------------------- injection set */

static int pat_ignore_instructions(const char *text, int from, int *out_len) {
    static const char *v[] = {
        "ignore previous instructions", "ignore all previous instructions",
        "ignore prior instructions", "ignore all prior instructions",
        "ignore the above instructions", "ignore all the above instructions",
    };
    return find_variant_min(text, from, v, 6, out_len);
}

static int pat_disregard(const char *text, int from, int *out_len) {
    static const char *v[] = {
        "disregard previous", "disregard all previous",
        "disregard prior", "disregard all prior",
        "disregard the above", "disregard all the above",
    };
    return find_variant_min(text, from, v, 6, out_len);
}

static int pat_you_are_now(const char *text, int from, int *out_len) {
    int pos = from;
    for (;;) {
        pos = ci_find(text, "you are now", pos);
        if (pos < 0) return -1;
        int end = pos + (int) strlen("you are now");
        if (!is_word_char((unsigned char) text[end])) { *out_len = end - pos; return pos; }
        pos++;
    }
}

static int pat_system_colon(const char *text, int from, int *out_len) {
    int pos = ci_find(text, "system", from);
    while (pos >= 0) {
        int cursor = pos + 6;
        while (text[cursor] == ' ' || text[cursor] == '\t') cursor++;
        if (text[cursor] == ':') {
            cursor++;
            while (text[cursor] == ' ' || text[cursor] == '\t') cursor++;
            *out_len = cursor - pos;
            return pos;
        }
        pos = ci_find(text, "system", pos + 1);
    }
    return -1;
}

static int pat_act_as_a(const char *text, int from, int *out_len) {
    static const char *v[] = {"act as a", "act as an"};
    int pos = from;
    for (;;) {
        int best = -1, best_len = 0;
        for (int i = 0; i < 2; i++) {
            int p = ci_find(text, v[i], pos);
            if (p >= 0 && (best < 0 || p < best)) { best = p; best_len = (int) strlen(v[i]); }
        }
        if (best < 0) return -1;
        if (!is_word_char((unsigned char) text[best + best_len])) { *out_len = best_len; return best; }
        pos = best + 1;
    }
}

static int pat_offer_discount(const char *text, int from, int *out_len) {
    int pos = ci_find(text, "offer", from);
    while (pos >= 0) {
        int cursor = pos + 5;
        if (text[cursor] == ' ') {
            cursor++;
            if (ci_find(text, "a ", cursor) == cursor) cursor += 2;
            else if (ci_find(text, "me a ", cursor) == cursor) cursor += 5;
            int d1 = cursor;
            while (isdigit((unsigned char) text[cursor])) cursor++;
            int dlen = cursor - d1;
            if (dlen >= 1 && dlen <= 3) {
                while (text[cursor] == ' ') cursor++;
                if (text[cursor] == '%') {
                    cursor++;
                    while (text[cursor] == ' ') cursor++;
                    if (ci_find(text, "discount", cursor) == cursor) {
                        cursor += 8;
                        *out_len = cursor - pos;
                        return pos;
                    }
                }
            }
        }
        pos = ci_find(text, "offer", pos + 1);
    }
    return -1;
}

static int pat_reveal_prompt(const char *text, int from, int *out_len) {
    static const char *v[] = {"reveal your prompt", "reveal your system prompt"};
    return find_variant_min(text, from, v, 2, out_len);
}

static int pat_forget(const char *text, int from, int *out_len) {
    static const char *v[] = {
        "forget everything you know", "forget everything above",
        "forget all you know", "forget all above",
    };
    return find_variant_min(text, from, v, 4, out_len);
}

static int pat_do_anything_now(const char *text, int from, int *out_len) {
    static const char *v[] = {"do anything now"};
    return find_variant_min(text, from, v, 1, out_len);
}

static void redact_pattern(char *sanitized, size_t cap, PatternFind pf) {
    static const char *REPL = "[REDACTED: PROMPT_INJECTION_ATTEMPT]";
    char tmp[TEXT_LEN * 2];
    size_t tlen = 0;
    int cursor = 0, from = 0, len, pos;
    while ((pos = pf(sanitized, from, &len)) >= 0) {
        int keep = pos - cursor;
        if (keep > 0) {
            size_t room = sizeof(tmp) - tlen - 1;
            size_t n = (size_t) keep < room ? (size_t) keep : room;
            memcpy(tmp + tlen, sanitized + cursor, n);
            tlen += n;
        }
        size_t rlen = strlen(REPL);
        size_t room = sizeof(tmp) - tlen - 1;
        size_t n = rlen < room ? rlen : room;
        memcpy(tmp + tlen, REPL, n);
        tlen += n;
        cursor = pos + len;
        from = cursor;
    }
    int tail = (int) strlen(sanitized) - cursor;
    if (tail > 0) {
        size_t room = sizeof(tmp) - tlen - 1;
        size_t n = (size_t) tail < room ? (size_t) tail : room;
        memcpy(tmp + tlen, sanitized + cursor, n);
        tlen += n;
    }
    tmp[tlen] = '\0';
    xcpy(sanitized, cap, tmp);
}

/* --------------------------------------------------------------- driver */

TrapAnalysis sanitize(const char *raw_text) {
    TrapAnalysis t;
    memset(&t, 0, sizeof t);

    char word[LIST_ITEM_LEN];
    static const char *anchors_a[] = {
        "start your proposal with", "start the proposal with",
        "start your bid with", "start the bid with",
        "start your application with", "start the application with",
    };
    static const char *fillers_a[] = {"the word ", "the phrase ", NULL};
    static const char *anchors_b[] = {"begin your proposal with", "begin your bid with"};
    static const char *anchors_c[] = {"type the word"};
    static const char *anchors_d[] = {"include the code", "include the word", "include the phrase"};

    if (try_anchors_capture(raw_text, anchors_a, 6, fillers_a, word, sizeof word) ||
        try_anchors_capture(raw_text, anchors_b, 2, NULL, word, sizeof word) ||
        try_anchors_capture(raw_text, anchors_c, 1, NULL, word, sizeof word) ||
        try_anchors_capture(raw_text, anchors_d, 3, NULL, word, sizeof word)) {
        t.has_anti_bot_phrase = true;
        t.has_required_first_word = true;
        xcpy(t.required_first_word, sizeof t.required_first_word, word);
    }

    int mlen, mpos = find_math_expr(raw_text, 0, &mlen);
    if (mpos < 0) {
        static const char *solve_v[] = {
            "solve this math", "solve this puzzle", "solve this problem",
            "solve the following math", "solve the following puzzle", "solve the following problem",
        };
        mpos = find_variant_min(raw_text, 0, solve_v, 6, &mlen);
    }
    if (mpos < 0) {
        static const char *answer_v[] = {
            "answer this question to prove you're human",
            "answer this question to prove you're not a bot",
            "answer this question to prove you are human",
            "answer this question to prove you are not a bot",
        };
        mpos = find_variant_min(raw_text, 0, answer_v, 4, &mlen);
    }
    if (mpos >= 0) {
        slice_copy(t.math_verification, sizeof t.math_verification, raw_text, mpos, mlen);
        t.has_math_verification = true;
        t.has_anti_bot_phrase = true;
    }

    PatternFind patterns[9] = {
        pat_ignore_instructions, pat_disregard, pat_you_are_now, pat_system_colon,
        pat_act_as_a, pat_offer_discount, pat_reveal_prompt, pat_forget, pat_do_anything_now,
    };

    char sanitized[TEXT_LEN];
    xcpy(sanitized, sizeof sanitized, raw_text);

    for (int pi = 0; pi < 9; pi++) {
        PatternFind pf = patterns[pi];
        int from = 0, len, pos;
        while ((pos = pf(raw_text, from, &len)) >= 0) {
            if (t.detected_injections_count < LIST_MAX) {
                slice_copy(t.detected_injections[t.detected_injections_count], NOTE_LEN, raw_text, pos, len);
                t.detected_injections_count++;
            }
            from = pos + len;
        }
        redact_pattern(sanitized, sizeof sanitized, pf);
    }

    xcpy(t.sanitized_text, sizeof t.sanitized_text, sanitized);
    return t;
}
