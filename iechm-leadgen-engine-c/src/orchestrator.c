#include "orchestrator.h"
#include "channels.h"
#include "filters.h"
#include "pipeline.h"
#include "rng.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SUBDOMAIN_SPAWN_THRESHOLD 5
#define SUBDOMAIN_DEPRECATE_THRESHOLD 2
#define DEGRADING_AGENT_PROBABILITY 0.08
#define CLEAN_AGENT_FAULT_RATE 0.02
#define DEGRADING_AGENT_FAULT_RATE 0.45

static double friction_penalty(const char *level) {
    if (strcmp(level, "LOW") == 0) return 0.0;
    if (strcmp(level, "MEDIUM") == 0) return 0.15;
    if (strcmp(level, "HIGH") == 0) return 0.35;
    return 0.15;
}

/* -------------------------------------------------------- agent registry */

static AgentRegistryEntry *registry_find(Orchestrator *o, const char *key) {
    for (int i = 0; i < o->agent_registry.count; i++) {
        AgentRegistryEntry *e = vec_at(&o->agent_registry, i);
        if (strcmp(e->key, key) == 0) return e;
    }
    return NULL;
}

static void assign_fault_bias(Orchestrator *o, const char *agent_id) {
    Rng r;
    rng_init_str(&r, agent_id);
    bool degrading = rng_next_double(&r) < DEGRADING_AGENT_PROBABILITY;
    FaultRateEntry *e = vec_push(&o->fault_rates);
    xcpy(e->agent_id, ID_LEN, agent_id);
    e->fault_rate = degrading ? DEGRADING_AGENT_FAULT_RATE : CLEAN_AGENT_FAULT_RATE;
}

static double get_fault_rate(Orchestrator *o, const char *agent_id) {
    for (int i = 0; i < o->fault_rates.count; i++) {
        FaultRateEntry *e = vec_at(&o->fault_rates, i);
        if (strcmp(e->agent_id, agent_id) == 0) return e->fault_rate;
    }
    return CLEAN_AGENT_FAULT_RATE;
}

static void spawn_agent_into(AgentNode *agent, AgentLayer layer, const char *role,
                              bool has_ct, ChannelType ct, bool has_sub, const char *sub_domain) {
    memset(agent, 0, sizeof *agent);
    new_id(agent->agent_id, sizeof agent->agent_id, "agent");
    agent->layer = layer;
    xcpy(agent->role, sizeof agent->role, role);
    agent->has_channel_type = has_ct;
    agent->channel_type = ct;
    agent->has_sub_domain = has_sub;
    xcpy(agent->sub_domain, sizeof agent->sub_domain, has_sub ? sub_domain : "");
    agent->status = AS_ACTIVE;
    xcpy(agent->model_signature, sizeof agent->model_signature, "primary-extractor-v1");
    agent->drift_score = 0.0;
    agent->consecutive_flags = 0;
    agent->leads_handled = 0;
    now_iso(agent->spawned_at, sizeof agent->spawned_at);
}

static AgentNode *registry_get_or_spawn(Orchestrator *o, const char *key, AgentLayer layer, const char *role,
                                         bool has_ct, ChannelType ct, bool has_sub, const char *sub_domain) {
    AgentRegistryEntry *e = registry_find(o, key);
    if (e && e->agent.status != AS_DEPRECATED) return &e->agent;

    if (!e) {
        e = vec_push(&o->agent_registry);
        xcpy(e->key, AGENT_KEY_LEN, key);
    }
    spawn_agent_into(&e->agent, layer, role, has_ct, ct, has_sub, sub_domain);
    assign_fault_bias(o, e->agent.agent_id);
    return &e->agent;
}

void orchestrator_init(Orchestrator *o) {
    strategy_ledger_init(&o->ledger);
    sentinel_init(&o->sentinel);
    vec_init(&o->agent_registry, sizeof(AgentRegistryEntry));
    vec_init(&o->fault_rates, sizeof(FaultRateEntry));
}

/* --------------------------------------------------------- per-channel run */

typedef struct { char combo[SHORT_LEN * 2]; char platform[SHORT_LEN]; char sub_domain[SHORT_LEN]; int count; } ComboCount;
#define COMBOS_MAX 64

static ComboCount *combo_find_or_add(ComboCount *combos, int *n, const char *platform, const char *sub_domain) {
    char combo[SHORT_LEN * 2];
    snprintf(combo, sizeof combo, "%s::%s", platform, sub_domain);
    for (int i = 0; i < *n; i++) if (strcmp(combos[i].combo, combo) == 0) return &combos[i];
    if (*n >= COMBOS_MAX) return &combos[COMBOS_MAX - 1];
    ComboCount *c = &combos[(*n)++];
    xcpy(c->combo, sizeof c->combo, combo);
    xcpy(c->platform, sizeof c->platform, platform);
    xcpy(c->sub_domain, sizeof c->sub_domain, sub_domain);
    c->count = 0;
    return c;
}

static void join_notes_commas(char notes[][NOTE_LEN], int count, char *out, size_t outcap) {
    out[0] = '\0';
    size_t used = 0;
    for (int i = 0; i < count; i++) {
        if (i > 0) { xcpy(out + used, outcap - used, ","); used = strlen(out); }
        xcpy(out + used, outcap - used, notes[i]);
        used = strlen(out);
    }
}

static ChannelCycleSummary run_channel(Orchestrator *o, Store *st, const ChannelAdapter *adapter, int day) {
    ChannelType ct = adapter->channel_type;

    char l2_key[AGENT_KEY_LEN], l2_role[ROLE_LEN];
    snprintf(l2_key, sizeof l2_key, "L2:%s", channel_type_str(ct));
    snprintf(l2_role, sizeof l2_role, "%s Channel Controller", channel_type_str(ct));
    AgentNode *l2 = registry_get_or_spawn(o, l2_key, AL_L2_MACRO_CHANNEL, l2_role, true, ct, false, "");
    store_save_agent(st, l2);

    Vec signals;
    vec_init(&signals, sizeof(RawSignal));
    adapter->generate_daily_signals(day, &signals);

    ComboCount combos[COMBOS_MAX];
    int combos_n = 0;

    int layer0_passed = 0, qualified = 0, bids_approved = 0, bids_rejected = 0;

    for (int i = 0; i < signals.count; i++) {
        RawSignal *signal = vec_at(&signals, i);

        char platform_key[AGENT_KEY_LEN], platform_role[ROLE_LEN];
        snprintf(platform_key, sizeof platform_key, "L3:%s:%s", channel_type_str(ct), signal->platform);
        snprintf(platform_role, sizeof platform_role, "%s Worker", signal->platform);
        AgentNode *worker = registry_get_or_spawn(o, platform_key, AL_L3_PLATFORM_WORKER, platform_role, true, ct, false, "");

        char responsible_key[AGENT_KEY_LEN];
        xcpy(responsible_key, sizeof responsible_key, platform_key);
        if (signal->sub_domain[0]) {
            char sub_key[AGENT_KEY_LEN];
            snprintf(sub_key, sizeof sub_key, "L4:%s:%s:%s", channel_type_str(ct), signal->platform, signal->sub_domain);
            AgentRegistryEntry *existing = registry_find(o, sub_key);
            if (existing && existing->agent.status == AS_ACTIVE) {
                xcpy(responsible_key, sizeof responsible_key, sub_key);
                worker = &existing->agent;
            }
        }

        double fault_rate = get_fault_rate(o, worker->agent_id);
        PipelineOutcome outcome;
        run_pipeline(signal, &o->ledger, fault_rate, &outcome);

        Layer0Result l0 = passes_layer0(signal);
        store_save_signal(st, signal, day, l0.passed, l0.reason);

        if (!outcome.has_lead) continue;

        layer0_passed++;
        qualified++;
        if (signal->sub_domain[0]) {
            ComboCount *c = combo_find_or_add(combos, &combos_n, signal->platform, signal->sub_domain);
            c->count++;
        }

        store_save_lead(st, &outcome.lead);

        if (outcome.has_bid) {
            store_save_bid(st, &outcome.bid);
            bool approved = strcmp(outcome.bid.reviewer_status, "APPROVED") == 0;
            bids_approved += approved ? 1 : 0;
            bids_rejected += approved ? 0 : 1;
            if (approved) strategy_record_outcome(&o->ledger, outcome.bid.strategy_id, true);

            worker->leads_handled++;
            bool anomaly = (!approved) || (outcome.bid.rewrite_count > 0);
            char reason[TEXT_LEN];
            if (anomaly) {
                char notes_joined[NOTES_MAX * NOTE_LEN];
                join_notes_commas(outcome.bid.reviewer_notes, outcome.bid.reviewer_notes_count, notes_joined, sizeof notes_joined);
                snprintf(reason, sizeof reason, "reviewer_flagged_draft (rewrites=%d, final=%s): %s",
                         outcome.bid.rewrite_count, outcome.bid.reviewer_status, notes_joined);
            } else {
                xcpy(reason, sizeof reason, "ok");
            }

            SentinelEvalResult sr = sentinel_evaluate(&o->sentinel, worker, anomaly, reason);

            if (sr.replaced) {
                store_save_agent(st, &sr.replacement);
                store_save_agent(st, worker); /* quarantined predecessor, distinct agent_id row */
                assign_fault_bias(o, sr.replacement.agent_id);
                AgentRegistryEntry *slot = registry_find(o, responsible_key);
                if (slot) slot->agent = sr.replacement;
            } else {
                store_save_agent(st, worker);
            }
            if (sr.has_event) store_save_sentinel_event(st, &sr.event);
        } else {
            store_save_agent(st, worker);
        }
    }

    ChannelCycleSummary summary;
    memset(&summary, 0, sizeof summary);
    xcpy(summary.channel_type, sizeof summary.channel_type, channel_type_str(ct));
    int platform_count;
    adapter->list_platforms(&platform_count);
    summary.platforms = platform_count;
    summary.signals = signals.count;
    summary.layer0_passed = layer0_passed;
    summary.qualified = qualified;
    summary.bids_approved = bids_approved;
    summary.bids_rejected = bids_rejected;

    for (int i = 0; i < combos_n; i++) {
        ComboCount *c = &combos[i];
        char key[AGENT_KEY_LEN];
        snprintf(key, sizeof key, "L4:%s:%s:%s", channel_type_str(ct), c->platform, c->sub_domain);
        AgentRegistryEntry *existing = registry_find(o, key);

        if (c->count >= SUBDOMAIN_SPAWN_THRESHOLD && (!existing || existing->agent.status != AS_ACTIVE)) {
            char role[ROLE_LEN];
            snprintf(role, sizeof role, "%s Micro-Worker", c->sub_domain);
            AgentNode *spawned = registry_get_or_spawn(o, key, AL_L4_SUBDOMAIN_WORKER, role, true, ct, true, c->sub_domain);
            store_save_agent(st, spawned);
            if (summary.spawned_count < CHANNEL_SPAWN_LIST_MAX)
                snprintf(summary.spawned[summary.spawned_count++], ROLE_LEN, "%s/%s", c->platform, c->sub_domain);
        } else if (existing && existing->agent.status == AS_ACTIVE && c->count < SUBDOMAIN_DEPRECATE_THRESHOLD) {
            existing->agent.status = AS_DEPRECATED;
            store_save_agent(st, &existing->agent);
            if (summary.deprecated_count < CHANNEL_SPAWN_LIST_MAX)
                snprintf(summary.deprecated[summary.deprecated_count++], ROLE_LEN, "%s/%s", c->platform, c->sub_domain);
        }
    }

    char created_at[SHORT_LEN];
    now_iso(created_at, sizeof created_at);
    store_save_run_log(st, day, channel_type_str(ct), signals.count, layer0_passed, qualified,
                        bids_approved, bids_rejected, created_at);

    double velocity = qualified / 20.0;
    if (velocity > 1.0) velocity = 1.0;
    double health = velocity - friction_penalty(adapter->friction_level());
    if (health < 0.0) health = 0.0;
    summary.channel_health_score = health;

    free(signals.items);
    return summary;
}

void orchestrator_run_cycle(Orchestrator *o, Store *st, int day, CycleSummary *out) {
    memset(out, 0, sizeof *out);

    AgentNode *central = registry_get_or_spawn(o, "L1:director", AL_L1_CENTRAL_COMMAND, "Central Director", false, 0, false, "");
    store_save_agent(st, central);

    int events_before = o->sentinel.event_count;

    for (int i = 0; i < channels_count(); i++) {
        const ChannelAdapter *adapter = channels_get(i);
        out->channels[out->channels_count++] = run_channel(o, st, adapter, day);
    }

    for (int i = 0; i < o->ledger.count; i++) store_save_strategy(st, &o->ledger.records[i]);

    out->day = day;
    out->sentinel_events_this_cycle = o->sentinel.event_count - events_before;
    for (int i = 0; i < out->channels_count; i++) {
        out->total_signals += out->channels[i].signals;
        out->total_qualified += out->channels[i].qualified;
        out->total_bids_approved += out->channels[i].bids_approved;
    }
}
