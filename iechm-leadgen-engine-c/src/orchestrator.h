/* The Director -- mirrors core/orchestrator.py. Runs one simulated day
 * across every channel adapter, owns the agent swarm (spawn/deprecate per
 * the 5-lead/day rule), routes each qualified signal through the pipeline,
 * and feeds every outcome to the Sentinel. */
#ifndef IECHM_ORCHESTRATOR_H
#define IECHM_ORCHESTRATOR_H

#include "models.h"
#include "strategist.h"
#include "sentinel.h"
#include "store.h"
#include "vec.h"

#define AGENT_KEY_LEN ROLE_LEN

typedef struct {
    char key[AGENT_KEY_LEN]; /* stable identity, e.g. "L4:B2B_TRADE_DIRECTORY:Alibaba:RFQ:Tooling" */
    AgentNode agent;
} AgentRegistryEntry;

typedef struct {
    char agent_id[ID_LEN];
    double fault_rate;
} FaultRateEntry;

typedef struct {
    StrategyLedger ledger;
    Sentinel sentinel;
    Vec agent_registry; /* AgentRegistryEntry, keyed by stable string identity */
    Vec fault_rates;    /* FaultRateEntry, keyed by agent_id */
} Orchestrator;

#define CHANNEL_SPAWN_LIST_MAX 32

typedef struct {
    char channel_type[SHORT_LEN];
    int platforms;
    int signals;
    int layer0_passed;
    int qualified;
    int bids_approved;
    int bids_rejected;
    char spawned[CHANNEL_SPAWN_LIST_MAX][ROLE_LEN];
    int spawned_count;
    char deprecated[CHANNEL_SPAWN_LIST_MAX][ROLE_LEN];
    int deprecated_count;
    double channel_health_score;
} ChannelCycleSummary;

typedef struct {
    int day;
    ChannelCycleSummary channels[5];
    int channels_count;
    int sentinel_events_this_cycle;
    int total_signals;
    int total_qualified;
    int total_bids_approved;
} CycleSummary;

void orchestrator_init(Orchestrator *o);
void orchestrator_run_cycle(Orchestrator *o, Store *st, int day, CycleSummary *out);

#endif
