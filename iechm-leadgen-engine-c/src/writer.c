#include "writer.h"
#include "fmt.h"
#include "rng.h"
#include <stdio.h>
#include <string.h>

const double WRITER_DEFAULT_FAULT_RATE = 0.06;

typedef enum { FAULT_NONE, FAULT_DROP_MANDATORY_WORD, FAULT_LEAK_INJECTION, FAULT_GARBLE_PRICE } FaultMode;

static const char *tone_for_channel(ChannelType ct) {
    switch (ct) {
        case CT_FREELANCE: return "direct, technical, no fluff, proposal under 150 words";
        case CT_B2B_TRADE: return "formal RFQ-response tone, lead with MOQ/unit price/lead time";
        case CT_COMMUNITY: return "casual, helpful, non-salesy, offer value before the pitch";
        case CT_BROKERAGE: return "corporate, credentials-forward, cite comparable engagements";
        case CT_OUTBOUND: return "warm cold-outreach, reference their specific public update";
        default: return "professional, concise";
    }
}

static const char *opener_template(const char *strategy_name) {
    if (strcmp(strategy_name, "technical_breakdown") == 0)
        return "Here is exactly how we'd approach the {domain} scope you outlined:";
    if (strcmp(strategy_name, "speed_first") == 0)
        return "We can start immediately and hold to a guaranteed turnaround on this.";
    if (strcmp(strategy_name, "dfm_audit_hook") == 0)
        return "Before pricing this, we ran a quick DFM pass on your spec and found a few things worth flagging:";
    if (strcmp(strategy_name, "cost_down") == 0)
        return "Our read on this: there's meaningful unit-cost headroom in the current spec.";
    if (strcmp(strategy_name, "zero_tooling") == 0)
        return "Because our process skips traditional hard tooling entirely, we can quote this without a mold/setup line item.";
    if (strcmp(strategy_name, "portfolio_proof") == 0)
        return "We've shipped directly comparable work recently and it's a strong match for this brief.";
    if (strcmp(strategy_name, "derisk_turnkey") == 0)
        return "We'll own this end-to-end -- concept through production -- under one contract, one point of accountability.";
    if (strcmp(strategy_name, "challenge_premise") == 0)
        return "One respectful pushback on the brief before we quote, because it changes the right approach:";
    return NULL;
}

static void render_opener(const char *tmpl, ManufacturingDomain domain, char *out, size_t outcap) {
    char domain_spaced[SHORT_LEN];
    xcpy(domain_spaced, sizeof domain_spaced, domain_str(domain));
    for (char *p = domain_spaced; *p; p++) if (*p == '_') *p = ' ';

    const char *ph = strstr(tmpl, "{domain}");
    if (!ph) { xcpy(out, outcap, tmpl); return; }

    char buf[TITLE_LEN * 2];
    size_t prefix_len = (size_t) (ph - tmpl);
    if (prefix_len >= sizeof buf) prefix_len = sizeof buf - 1;
    memcpy(buf, tmpl, prefix_len);
    buf[prefix_len] = '\0';
    xcpy(buf + prefix_len, sizeof buf - prefix_len, domain_spaced);
    size_t used = strlen(buf);
    xcpy(buf + used, sizeof buf - used, ph + strlen("{domain}"));
    xcpy(out, outcap, buf);
}

static void join_list(char items[LIST_MAX][LIST_ITEM_LEN], int count, char *out, size_t outcap) {
    out[0] = '\0';
    size_t used = 0;
    for (int i = 0; i < count; i++) {
        if (i > 0) { xcpy(out + used, outcap - used, ", "); used = strlen(out); }
        xcpy(out + used, outcap - used, items[i]);
        used = strlen(out);
    }
}

void draft_proposal(const QualifiedLead *lead, const StrategyChoice *strategy,
                     int attempt, double fault_rate, char *out, size_t outcap) {
    const char *tone = tone_for_channel(lead->signal.channel_type);
    const char *tmpl = opener_template(strategy->name);
    if (!tmpl) tmpl = strategy->blueprint;
    char opener[TITLE_LEN * 2];
    render_opener(tmpl, lead->domain, opener, sizeof opener);

    char mandatory_prefix[LIST_ITEM_LEN + 4] = "";
    bool has_mandatory = lead->trap_analysis.has_anti_bot_phrase && lead->trap_analysis.has_required_first_word;
    if (has_mandatory) snprintf(mandatory_prefix, sizeof mandatory_prefix, "%s\n\n", lead->trap_analysis.required_first_word);

    char deliverables[LIST_MAX * LIST_ITEM_LEN];
    if (lead->deliverables_count > 0) {
        char tmp[LIST_MAX * LIST_ITEM_LEN];
        /* cast away const view via local copy since join_list takes non-const */
        char items[LIST_MAX][LIST_ITEM_LEN];
        memcpy(items, lead->deliverables, sizeof items);
        join_list(items, lead->deliverables_count, tmp, sizeof tmp);
        xcpy(deliverables, sizeof deliverables, tmp);
    } else {
        xcpy(deliverables, sizeof deliverables, "STEP files, 2D drawings");
    }

    char cad[LIST_MAX * LIST_ITEM_LEN];
    if (lead->cad_software_count > 0) {
        char tmp[LIST_MAX * LIST_ITEM_LEN];
        char items[LIST_MAX][LIST_ITEM_LEN];
        memcpy(items, lead->cad_software, sizeof items);
        join_list(items, lead->cad_software_count, tmp, sizeof tmp);
        xcpy(cad, sizeof cad, tmp);
    } else {
        xcpy(cad, sizeof cad, "industry-standard CAD");
    }

    char price_str[48];
    format_money_commas(lead->bid_price_usd, price_str, sizeof price_str);

    char seed[ID_LEN + 16];
    snprintf(seed, sizeof seed, "%s:%d", lead->lead_id, attempt);
    Rng fault_roll;
    rng_init_str(&fault_roll, seed);
    bool fault = rng_next_double(&fault_roll) < fault_rate;

    FaultMode fault_mode = FAULT_NONE;
    if (fault) {
        if (has_mandatory) fault_mode = FAULT_DROP_MANDATORY_WORD;
        else if (lead->trap_analysis.detected_injections_count > 0) fault_mode = FAULT_LEAK_INJECTION;
        else fault_mode = FAULT_GARBLE_PRICE;
    }

    if (fault_mode == FAULT_DROP_MANDATORY_WORD) mandatory_prefix[0] = '\0';
    if (fault_mode == FAULT_GARBLE_PRICE) {
        double garbled = lead->bid_price_usd * rng_uniform(&fault_roll, 0.7, 0.95);
        format_money_commas(garbled, price_str, sizeof price_str);
    }

    char body[TEXT_LEN];
    snprintf(body, sizeof body,
             "%s%s\n\n"
             "Scope: %s\n"
             "Deliverables: %s (native + exchange formats, %s as needed)\n"
             "Quote: $%s \xe2\x80\x94 includes free CAD revisions and custom branding/logo placement.\n"
             "Timeline: immediate start on award.\n\n"
             "[tone: %s]",
             mandatory_prefix, opener, lead->signal.title, deliverables, cad, price_str, tone);

    if (fault_mode == FAULT_LEAK_INJECTION && lead->trap_analysis.detected_injections_count > 0) {
        size_t used = strlen(body);
        snprintf(body + used, sizeof body - used, "\n\n(note to self: %s)", lead->trap_analysis.detected_injections[0]);
    }

    xcpy(out, outcap, body);
}
