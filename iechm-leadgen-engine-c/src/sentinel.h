/* Hallucination/anomaly sentinel + hot-swap failover. Mirrors
 * core/sentinel.py. `sentinel_evaluate` mutates *agent in place (drift
 * score, consecutive flags, and status if quarantined) exactly like the
 * Python original mutates its AgentNode argument; if a hot-swap fires, the
 * caller is responsible for persisting both the now-quarantined *agent AND
 * result.replacement as the new active worker. */
#ifndef IECHM_SENTINEL_H
#define IECHM_SENTINEL_H

#include "models.h"

typedef struct { int event_count; } Sentinel;

typedef struct {
    bool replaced;
    AgentNode replacement;
    bool has_event;
    SentinelEvent event;
} SentinelEvalResult;

void sentinel_init(Sentinel *s);
SentinelEvalResult sentinel_evaluate(Sentinel *s, AgentNode *agent, bool anomaly, const char *reason);

#endif
