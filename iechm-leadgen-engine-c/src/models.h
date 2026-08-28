/* Core data model. Mirrors core/models.py: everything downstream (channels,
 * pipeline, orchestrator, server, store) speaks these types. Enum values
 * carry a canonical string form (xxx_str()) matching the Python str-Enum
 * `.value` exactly, since those strings are what actually gets persisted
 * and shipped to the frontend -- the frontend keys its rendering (status
 * pill colors, layer labels) off these exact literals.
 */
#ifndef IECHM_MODELS_H
#define IECHM_MODELS_H

#include "common.h"

typedef enum {
    CT_FREELANCE, CT_B2B_TRADE, CT_COMMUNITY, CT_BROKERAGE, CT_OUTBOUND, CT_COUNT
} ChannelType;
const char *channel_type_str(ChannelType t);

typedef enum {
    ARCH_NPD_INNOVATOR, ARCH_MIDDLEMAN_RESELLER, ARCH_SME_ENGINEERING,
    ARCH_CROWDFUNDER, ARCH_INSTITUTIONAL, ARCH_UNKNOWN
} ClientArchetype;
const char *archetype_str(ClientArchetype a);

typedef enum {
    DOM_CAD_MECHANICAL, DOM_PCB_ELECTRONICS, DOM_ENCLOSURE_DESIGN,
    DOM_PROTOTYPING_3D_PRINT, DOM_DFM_INJECTION_MOLDING, DOM_SHEET_METAL,
    DOM_FULL_NPD_TURNKEY, DOM_UNKNOWN, DOM_COUNT
} ManufacturingDomain;
const char *domain_str(ManufacturingDomain d);

typedef enum {
    LS_REJECTED_LAYER0, LS_REJECTED_QUALIFICATION, LS_QUALIFIED,
    LS_BID_DRAFTED, LS_BID_APPROVED, LS_BID_REJECTED_BY_REVIEWER
} LeadStatus;
const char *lead_status_str(LeadStatus s);

typedef enum { AS_ACTIVE, AS_IDLE, AS_QUARANTINED, AS_DEPRECATED } AgentStatus;
const char *agent_status_str(AgentStatus s);

typedef enum {
    AL_L1_CENTRAL_COMMAND, AL_L2_MACRO_CHANNEL, AL_L3_PLATFORM_WORKER,
    AL_L4_SUBDOMAIN_WORKER, AL_SENTINEL
} AgentLayer;
const char *agent_layer_str(AgentLayer l);

/* ---------------------------------------------------------------- ids/time */

void new_id(char *dst, size_t cap, const char *prefix);
void now_iso(char *dst, size_t cap);

/* ---------------------------------------------------------------- records */

typedef struct {
    bool has_anti_bot_phrase;
    bool has_required_first_word;
    char required_first_word[LIST_ITEM_LEN];
    char detected_injections[LIST_MAX][NOTE_LEN];
    int detected_injections_count;
    bool has_math_verification;
    char math_verification[NOTE_LEN];
    char sanitized_text[TEXT_LEN];
} TrapAnalysis;

/* Renders TrapAnalysis.to_dict() as JSON text into dst (caller-owned buffer). */
void trap_analysis_to_json(const TrapAnalysis *t, char *dst, size_t cap);

typedef struct {
    char signal_id[ID_LEN];
    ChannelType channel_type;
    char platform[SHORT_LEN];
    char sub_domain[SHORT_LEN]; /* "" if none */
    char title[TITLE_LEN];
    char raw_text[TEXT_LEN];
    bool has_budget;
    double stated_budget_usd;
    bool has_volume;
    int target_volume;
    char url[URL_LEN];
    char posted_at[SHORT_LEN];
} RawSignal;

typedef struct {
    char lead_id[ID_LEN];
    RawSignal signal;
    ClientArchetype client_archetype;
    ManufacturingDomain domain;
    char cad_software[LIST_MAX][LIST_ITEM_LEN];
    int cad_software_count;
    char materials[LIST_MAX][LIST_ITEM_LEN];
    int materials_count;
    char deliverables[LIST_MAX][LIST_ITEM_LEN];
    int deliverables_count;
    char project_stage[SHORT_LEN];
    bool has_target_volume;
    int target_volume;
    double market_price_usd;
    double bid_price_usd;
    double cogs_usd;
    double margin_pct;
    double qualification_score;
    TrapAnalysis trap_analysis;
    LeadStatus status;
    char created_at[SHORT_LEN];
} QualifiedLead;

typedef struct {
    char bid_id[ID_LEN];
    char lead_id[ID_LEN];
    char strategy_id[STRATEGY_ID_LEN];
    char strategy_name[SHORT_LEN];
    bool explore;
    char proposal_text[TEXT_LEN];
    double price_usd;
    char reviewer_status[SHORT_LEN]; /* APPROVED | REJECTED */
    char reviewer_notes[NOTES_MAX][NOTE_LEN];
    int reviewer_notes_count;
    int rewrite_count;
    char created_at[SHORT_LEN];
} BidRecord;

typedef struct {
    char agent_id[ID_LEN];
    AgentLayer layer;
    char role[ROLE_LEN];
    bool has_channel_type;
    ChannelType channel_type;
    bool has_sub_domain;
    char sub_domain[SHORT_LEN];
    AgentStatus status;
    char model_signature[SHORT_LEN];
    double drift_score;
    int consecutive_flags;
    int leads_handled;
    char spawned_at[SHORT_LEN];
} AgentNode;

typedef struct {
    char event_id[ID_LEN];
    char agent_id[ID_LEN];
    char event_type[SHORT_LEN]; /* DRIFT_FLAG | HOTSWAP_TRIGGERED | RESUMED */
    double drift_score;
    char reason[TEXT_LEN];
    bool has_replacement;
    char replacement_agent_id[ID_LEN];
    char created_at[SHORT_LEN];
} SentinelEvent;

typedef struct {
    char strategy_id[STRATEGY_ID_LEN];
    ManufacturingDomain domain;
    char name[SHORT_LEN];
    int wins;
    int losses;
    char status[SHORT_LEN];
} StrategyRecord;

static inline double strategy_win_rate(const StrategyRecord *r) {
    int total = r->wins + r->losses;
    return total ? (double) r->wins / (double) total : 0.0;
}

#endif
