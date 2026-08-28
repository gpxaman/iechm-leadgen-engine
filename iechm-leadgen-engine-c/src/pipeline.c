#include "pipeline.h"
#include "filters.h"
#include "security.h"
#include "classify.h"
#include "pricing.h"
#include "writer.h"
#include "reviewer.h"
#include <stdio.h>
#include <string.h>

const double PIPELINE_DEFAULT_FAULT_RATE = 0.02;
static const double QUALIFICATION_SCORE_FLOOR = 0.35;

static double qualification_score(ManufacturingDomain domain, ClientArchetype archetype,
                                   const TrapAnalysis *trap, const PriceQuote *price) {
    double score = 0.5;
    if (domain != DOM_UNKNOWN) score += 0.2;
    if (archetype != ARCH_UNKNOWN) score += 0.1;
    if (price->margin_pct >= 40.0) score += 0.15;
    else if (price->floor_violation) score -= 0.25;
    if (trap->detected_injections_count > 0) score -= 0.05;
    return clampd(score, 0.0, 1.0);
}

void run_pipeline(const RawSignal *signal, StrategyLedger *ledger, double fault_rate, PipelineOutcome *out) {
    memset(out, 0, sizeof *out);

    Layer0Result layer0 = passes_layer0(signal);
    if (!layer0.passed) {
        out->rejected_stage = "LAYER0";
        xcpy(out->rejected_reason, sizeof out->rejected_reason, layer0.reason);
        return;
    }

    TrapAnalysis trap = sanitize(signal->raw_text);
    ManufacturingDomain domain = classify_domain(signal->raw_text);
    ClientArchetype archetype = classify_archetype(signal);

    char cad[LIST_MAX][LIST_ITEM_LEN]; int cad_n;
    extract_cad_software(signal->raw_text, cad, &cad_n);
    char materials[LIST_MAX][LIST_ITEM_LEN]; int materials_n;
    extract_materials(signal->raw_text, materials, &materials_n);
    char deliverables[LIST_MAX][LIST_ITEM_LEN]; int deliverables_n;
    extract_deliverables(signal->raw_text, deliverables, &deliverables_n);
    const char *stage = classify_project_stage(signal->raw_text);

    PriceQuote price = price_quote(domain, signal->has_volume, signal->target_volume,
                                    signal->has_budget, signal->stated_budget_usd);
    double score = qualification_score(domain, archetype, &trap, &price);

    if (score < QUALIFICATION_SCORE_FLOOR) {
        out->rejected_stage = "QUALIFICATION";
        snprintf(out->rejected_reason, sizeof out->rejected_reason, "score_%.2f_below_floor", score);
        return;
    }

    QualifiedLead *lead = &out->lead;
    new_id(lead->lead_id, sizeof lead->lead_id, "lead");
    lead->signal = *signal;
    lead->client_archetype = archetype;
    lead->domain = domain;
    memcpy(lead->cad_software, cad, sizeof cad); lead->cad_software_count = cad_n;
    memcpy(lead->materials, materials, sizeof materials); lead->materials_count = materials_n;
    memcpy(lead->deliverables, deliverables, sizeof deliverables); lead->deliverables_count = deliverables_n;
    xcpy(lead->project_stage, sizeof lead->project_stage, stage);
    lead->has_target_volume = signal->has_volume;
    lead->target_volume = signal->target_volume;
    lead->market_price_usd = price.market_price_usd;
    lead->bid_price_usd = price.bid_price_usd;
    lead->cogs_usd = price.cogs_usd;
    lead->margin_pct = price.margin_pct;
    lead->qualification_score = score;
    lead->trap_analysis = trap;
    lead->status = LS_QUALIFIED;
    now_iso(lead->created_at, sizeof lead->created_at);
    out->has_lead = true;

    StrategyChoice strategy = strategy_choose(ledger, domain);
    int rewrite_count = 0;
    char proposal_text[TEXT_LEN];
    draft_proposal(lead, &strategy, rewrite_count, fault_rate, proposal_text, sizeof proposal_text);
    ReviewResult result = review_proposal(lead, proposal_text, rewrite_count);

    while (!result.approved && result.needs_rewrite && rewrite_count < 2) {
        rewrite_count++;
        draft_proposal(lead, &strategy, rewrite_count, fault_rate, proposal_text, sizeof proposal_text);
        result = review_proposal(lead, proposal_text, rewrite_count);
    }

    lead->status = result.approved ? LS_BID_APPROVED : LS_BID_REJECTED_BY_REVIEWER;

    BidRecord *bid = &out->bid;
    new_id(bid->bid_id, sizeof bid->bid_id, "bid");
    xcpy(bid->lead_id, sizeof bid->lead_id, lead->lead_id);
    xcpy(bid->strategy_id, sizeof bid->strategy_id, strategy.strategy_id);
    xcpy(bid->strategy_name, sizeof bid->strategy_name, strategy.name);
    bid->explore = strategy.explore;
    xcpy(bid->proposal_text, sizeof bid->proposal_text, proposal_text);
    bid->price_usd = lead->bid_price_usd;
    xcpy(bid->reviewer_status, sizeof bid->reviewer_status, result.approved ? "APPROVED" : "REJECTED");
    for (int i = 0; i < result.notes_count && i < NOTES_MAX; i++)
        xcpy(bid->reviewer_notes[i], NOTE_LEN, result.notes[i]);
    bid->reviewer_notes_count = result.notes_count;
    bid->rewrite_count = rewrite_count;
    now_iso(bid->created_at, sizeof bid->created_at);
    out->has_bid = true;
}
