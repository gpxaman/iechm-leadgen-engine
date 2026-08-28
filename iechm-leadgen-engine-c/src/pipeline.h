/* Per-lead pipeline: Layer 0 -> Sanitizer -> classify -> price -> Strategist
 * -> Writer -> Reviewer (bounded rewrite loop). Mirrors core/pipeline.py. */
#ifndef IECHM_PIPELINE_H
#define IECHM_PIPELINE_H

#include "models.h"
#include "strategist.h"

extern const double PIPELINE_DEFAULT_FAULT_RATE; /* 0.02 */

typedef struct {
    bool has_lead;
    QualifiedLead lead;
    bool has_bid;
    BidRecord bid;
    const char *rejected_stage;  /* "LAYER0" | "QUALIFICATION" | NULL, static literal */
    char rejected_reason[SHORT_LEN];
} PipelineOutcome;

void run_pipeline(const RawSignal *signal, StrategyLedger *ledger, double fault_rate, PipelineOutcome *out);

#endif
