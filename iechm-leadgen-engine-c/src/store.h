/* SQLite-less persistence layer -- mirrors core/db.py's role ("the only
 * module that touches storage directly") but not its mechanism: this build
 * has no sqlite3 development headers available and the rewrite is meant to
 * carry zero external dependencies, so each table is an in-memory growable
 * array (see vec.h), periodically flushed to a small custom binary file
 * (store_save_to_disk/store_load_from_disk) so state survives a server
 * restart the same way the original's iechm.db file did. The binary format
 * is this process's own struct layout, not a portable file format -- an
 * equivalent tradeoff to the original being an sqlite-specific file.
 *
 * Every row carries a `seq`, a monotonically increasing insert counter.
 * core/db.py's reads rely on SQLite's ORDER BY tiebreaking behavior for
 * rows with identical timestamps (very common here: now_iso() has
 * one-second resolution and a single cycle can insert hundreds of rows
 * within the same second); `seq` gives "most recently inserted first" a
 * precise, deterministic meaning instead of leaving it to chance.
 *
 * Every JSON-shaped read is exposed as a *_json function that writes
 * directly into a Jsonw buffer, matching exactly what core/db.py's SQL
 * SELECTs (including its join column lists) hand back as dicts -- this
 * keeps the "which columns end up in which endpoint's response" knowledge
 * in one place, the same way the original's SQL text was that place.
 */
#ifndef IECHM_STORE_H
#define IECHM_STORE_H

#include "models.h"
#include "jsonw.h"
#include "vec.h"

typedef struct {
    uint64_t seq;
    char signal_id[ID_LEN];
    char channel_type[SHORT_LEN];
    char platform[SHORT_LEN];
    char sub_domain[SHORT_LEN];
    char title[TITLE_LEN];
    char raw_text[TEXT_LEN];
    bool has_budget;
    double stated_budget_usd;
    bool has_volume;
    int target_volume;
    char url[URL_LEN];
    char posted_at[SHORT_LEN];
    int layer0_passed;
    char layer0_reason[SHORT_LEN];
    int day;
} StoredSignal;

typedef struct {
    uint64_t seq;
    char lead_id[ID_LEN];
    char signal_id[ID_LEN];
    char client_archetype[SHORT_LEN];
    char domain[SHORT_LEN];
    char cad_software_json[JSONTEXT_LEN];
    char materials_json[JSONTEXT_LEN];
    char deliverables_json[JSONTEXT_LEN];
    char project_stage[SHORT_LEN];
    bool has_target_volume;
    int target_volume;
    double market_price_usd, bid_price_usd, cogs_usd, margin_pct, qualification_score;
    char trap_json[TEXT_LEN];
    char status[SHORT_LEN];
    char created_at[SHORT_LEN];
} StoredLead;

typedef struct {
    uint64_t seq;
    char bid_id[ID_LEN];
    char lead_id[ID_LEN];
    char strategy_id[STRATEGY_ID_LEN];
    char strategy_name[SHORT_LEN];
    int explore;
    char proposal_text[TEXT_LEN];
    double price_usd;
    char reviewer_status[SHORT_LEN];
    char reviewer_notes_json[JSONTEXT_LEN];
    int rewrite_count;
    char created_at[SHORT_LEN];
} StoredBid;

typedef struct {
    uint64_t seq;
    char agent_id[ID_LEN];
    char layer[SHORT_LEN];
    char role[ROLE_LEN];
    char channel_type[SHORT_LEN]; /* "" if none */
    char sub_domain[SHORT_LEN];   /* "" if none */
    char status[SHORT_LEN];
    char model_signature[SHORT_LEN];
    double drift_score;
    int consecutive_flags;
    int leads_handled;
    char spawned_at[SHORT_LEN];
} StoredAgent;

typedef struct {
    uint64_t seq;
    char event_id[ID_LEN];
    char agent_id[ID_LEN];
    char event_type[SHORT_LEN];
    double drift_score;
    char reason[TEXT_LEN];
    char replacement_agent_id[ID_LEN]; /* "" if none */
    char created_at[SHORT_LEN];
} StoredSentinelEvent;

typedef struct {
    uint64_t seq;
    char strategy_id[STRATEGY_ID_LEN];
    char domain[SHORT_LEN];
    char name[SHORT_LEN];
    int wins, losses;
    char status[SHORT_LEN];
} StoredStrategy;

typedef struct {
    uint64_t seq;
    int day;
    char channel_type[SHORT_LEN];
    int signals_count, layer0_passed, qualified, bids_approved, bids_rejected;
    char created_at[SHORT_LEN];
} RunLogEntry;

typedef struct {
    Vec signals, leads, bids, agents, sentinel_events, strategies, run_log;
    uint64_t next_seq;
} Store;

void store_init(Store *st);
void store_reset(Store *st);

void store_save_signal(Store *st, const RawSignal *signal, int day, bool layer0_passed, const char *layer0_reason);
void store_save_lead(Store *st, const QualifiedLead *lead);
void store_save_bid(Store *st, const BidRecord *bid);
void store_save_agent(Store *st, const AgentNode *agent); /* upsert by agent_id */
void store_save_sentinel_event(Store *st, const SentinelEvent *event);
void store_save_strategy(Store *st, const StrategyRecord *rec); /* upsert by strategy_id */
void store_save_run_log(Store *st, int day, const char *channel_type, int signals, int layer0_passed,
                         int qualified, int approved, int rejected, const char *created_at);

/* Each writes a complete JSON value (array/object) for the corresponding
 * API route; server.c wraps the result as the HTTP body directly. */
void store_write_leads_json(const Store *st, Jsonw *w, int limit, const char *status_filter, const char *channel_type_filter);
bool store_write_lead_detail_json(const Store *st, Jsonw *w, const char *lead_id); /* false if not found */
void store_write_agents_json(const Store *st, Jsonw *w);
void store_write_sentinel_events_json(const Store *st, Jsonw *w, int limit);
void store_write_strategies_json(const Store *st, Jsonw *w);
void store_write_funnel_json(const Store *st, Jsonw *w);

/* Custom binary snapshot -- see file header comment. Returns false (load
 * only) if no file exists yet, which is a normal first run, not an error. */
bool store_load_from_disk(Store *st, const char *path);
void store_save_to_disk(const Store *st, const char *path);

#endif
