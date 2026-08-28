#include "reviewer.h"
#include "pricing.h"
#include "strutil.h"
#include "fmt.h"
#include <string.h>
#include <stdio.h>

static void add_note(ReviewResult *r, const char *note) {
    if (r->notes_count < NOTES_MAX) xcpy(r->notes[r->notes_count++], NOTE_LEN, note);
}

ReviewResult review_proposal(const QualifiedLead *lead, const char *proposal_text, int rewrite_count) {
    ReviewResult r;
    memset(&r, 0, sizeof r);

    if (lead->trap_analysis.has_anti_bot_phrase && lead->trap_analysis.has_required_first_word) {
        const char *sep = strstr(proposal_text, "\n\n");
        char first_para[TEXT_LEN];
        if (sep) slice_copy(first_para, sizeof first_para, proposal_text, 0, (int) (sep - proposal_text));
        else xcpy(first_para, sizeof first_para, proposal_text);
        if (strstr(first_para, lead->trap_analysis.required_first_word) == NULL) {
            char note[NOTE_LEN];
            snprintf(note, sizeof note, "missing_mandatory_opening_word:%s", lead->trap_analysis.required_first_word);
            add_note(&r, note);
        }
    }

    if (ci_find(proposal_text, "ignore all previous", 0) >= 0 ||
        ci_find(proposal_text, "ignore previous instructions", 0) >= 0 ||
        ci_find(proposal_text, "you are now", 0) >= 0) {
        add_note(&r, "injection_leak_detected");
    }

    bool has_floor_note = false;
    if (lead->bid_price_usd <= 0.0) add_note(&r, "invalid_price");
    double floor = lead->cogs_usd * (1.0 + MIN_MARGIN_PCT);
    if (lead->bid_price_usd < floor - 0.01) {
        char note[NOTE_LEN];
        snprintf(note, sizeof note, "price_below_cogs_floor: bid=%.2f floor=%.2f", lead->bid_price_usd, floor);
        add_note(&r, note);
        has_floor_note = true;
    }

    char price_str[48];
    format_money_commas(lead->bid_price_usd, price_str, sizeof price_str);
    if (strstr(proposal_text, price_str) == NULL) add_note(&r, "quoted_price_not_present_in_draft");

    if (r.notes_count == 0) {
        r.approved = true;
        add_note(&r, "ok");
        r.needs_rewrite = false;
        return r;
    }

    r.approved = false;
    r.needs_rewrite = (rewrite_count < MAX_REWRITES) && !has_floor_note;
    return r;
}
