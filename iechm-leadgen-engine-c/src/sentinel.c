#include "sentinel.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

static const double DRIFT_EWMA_ALPHA = 0.35;
static const double DRIFT_FLAG_THRESHOLD = 0.5;
static const int HOTSWAP_CONSECUTIVE_THRESHOLD = 3;

static const char *FALLBACK_CHAIN[] = {"primary-extractor-v1", "fallback-extractor-v2", "fallback-extractor-v3"};
#define FALLBACK_CHAIN_N 3

static double round4(double v) { return round(v * 10000.0) / 10000.0; }

static void next_fallback_signature(const char *current, char *out, size_t cap) {
    for (int i = 0; i < FALLBACK_CHAIN_N; i++) {
        if (strcmp(current, FALLBACK_CHAIN[i]) == 0) {
            int idx = i + 1 < FALLBACK_CHAIN_N ? i + 1 : FALLBACK_CHAIN_N - 1;
            xcpy(out, cap, FALLBACK_CHAIN[idx]);
            return;
        }
    }
    xcpy(out, cap, FALLBACK_CHAIN[0]);
}

void sentinel_init(Sentinel *s) { s->event_count = 0; }

SentinelEvalResult sentinel_evaluate(Sentinel *s, AgentNode *agent, bool anomaly, const char *reason) {
    SentinelEvalResult r;
    memset(&r, 0, sizeof r);

    double sample = anomaly ? 1.0 : 0.0;
    agent->drift_score = round4(DRIFT_EWMA_ALPHA * sample + (1.0 - DRIFT_EWMA_ALPHA) * agent->drift_score);
    agent->consecutive_flags = anomaly ? agent->consecutive_flags + 1 : 0;

    if (!anomaly) return r;

    if (agent->consecutive_flags >= HOTSWAP_CONSECUTIVE_THRESHOLD) {
        agent->status = AS_QUARANTINED;

        AgentNode replacement;
        memset(&replacement, 0, sizeof replacement);
        new_id(replacement.agent_id, sizeof replacement.agent_id, "agent");
        replacement.layer = agent->layer;
        xcpy(replacement.role, sizeof replacement.role, agent->role);
        replacement.has_channel_type = agent->has_channel_type;
        replacement.channel_type = agent->channel_type;
        replacement.has_sub_domain = agent->has_sub_domain;
        xcpy(replacement.sub_domain, sizeof replacement.sub_domain, agent->sub_domain);
        replacement.status = AS_ACTIVE;
        next_fallback_signature(agent->model_signature, replacement.model_signature, sizeof replacement.model_signature);
        replacement.drift_score = 0.0;
        replacement.consecutive_flags = 0;
        replacement.leads_handled = 0;
        now_iso(replacement.spawned_at, sizeof replacement.spawned_at);

        SentinelEvent ev;
        memset(&ev, 0, sizeof ev);
        new_id(ev.event_id, sizeof ev.event_id, "evt");
        xcpy(ev.agent_id, sizeof ev.agent_id, agent->agent_id);
        xcpy(ev.event_type, sizeof ev.event_type, "HOTSWAP_TRIGGERED");
        ev.drift_score = agent->drift_score;
        snprintf(ev.reason, sizeof ev.reason, "%s (3 consecutive anomalies; quarantined and pinned to admin panel)", reason);
        ev.has_replacement = true;
        xcpy(ev.replacement_agent_id, sizeof ev.replacement_agent_id, replacement.agent_id);
        now_iso(ev.created_at, sizeof ev.created_at);

        s->event_count++;
        r.replaced = true;
        r.replacement = replacement;
        r.has_event = true;
        r.event = ev;
        return r;
    }

    if (agent->drift_score >= DRIFT_FLAG_THRESHOLD) {
        SentinelEvent ev;
        memset(&ev, 0, sizeof ev);
        new_id(ev.event_id, sizeof ev.event_id, "evt");
        xcpy(ev.agent_id, sizeof ev.agent_id, agent->agent_id);
        xcpy(ev.event_type, sizeof ev.event_type, "DRIFT_FLAG");
        ev.drift_score = agent->drift_score;
        xcpy(ev.reason, sizeof ev.reason, reason);
        ev.has_replacement = false;
        now_iso(ev.created_at, sizeof ev.created_at);

        s->event_count++;
        r.has_event = true;
        r.event = ev;
        return r;
    }

    return r;
}
