#include "strategist.h"
#include "rng.h"
#include <stdio.h>
#include <string.h>

static const double EXPLORE_RATE = 0.20;

typedef struct { const char *name; const char *blueprint; } StrategyDef;

static const StrategyDef CAD_MECHANICAL_STRATS[] = {
    {"technical_breakdown", "Lead with a detailed technical breakdown and tolerance callouts"},
    {"speed_first", "Lead with guaranteed turnaround time and immediate start"},
    {"challenge_premise", "Politely challenge an assumption in the brief to demonstrate expertise"},
};
static const StrategyDef PCB_ELECTRONICS_STRATS[] = {
    {"dfm_audit_hook", "Open with a free rapid DFM/EMC risk audit of their stated spec"},
    {"speed_first", "Lead with guaranteed turnaround time and immediate start"},
    {"portfolio_proof", "Lead with directly comparable past PCB layout outcomes"},
};
static const StrategyDef DFM_INJECTION_MOLDING_STRATS[] = {
    {"dfm_audit_hook", "Open with a free rapid DFM/EMC risk audit of their stated spec"},
    {"cost_down", "Lead with unit-cost reduction (VAVE) and tooling-cycle optimization"},
    {"zero_tooling", "Lead with zero-tooling-cost monolithic manufacturing capability"},
};
static const StrategyDef FULL_NPD_TURNKEY_STRATS[] = {
    {"derisk_turnkey", "Sell de-risking: full concept-to-production ownership in one contract"},
    {"technical_breakdown", "Lead with a detailed technical breakdown and tolerance callouts"},
};
static const StrategyDef FALLBACK_STRATS[] = {
    {"technical_breakdown", "Lead with a detailed technical breakdown and tolerance callouts"},
    {"speed_first", "Lead with guaranteed turnaround time and immediate start"},
    {"cost_down", "Lead with unit-cost reduction and quality assurance"},
};

static const StrategyDef *candidates_for(ManufacturingDomain domain, int *n) {
    switch (domain) {
        case DOM_CAD_MECHANICAL: *n = 3; return CAD_MECHANICAL_STRATS;
        case DOM_PCB_ELECTRONICS: *n = 3; return PCB_ELECTRONICS_STRATS;
        case DOM_DFM_INJECTION_MOLDING: *n = 3; return DFM_INJECTION_MOLDING_STRATS;
        case DOM_FULL_NPD_TURNKEY: *n = 2; return FULL_NPD_TURNKEY_STRATS;
        default: *n = 3; return FALLBACK_STRATS;
    }
}

static StrategyRecord *ledger_find(StrategyLedger *sl, const char *sid) {
    for (int i = 0; i < sl->count; i++)
        if (strcmp(sl->records[i].strategy_id, sid) == 0) return &sl->records[i];
    return NULL;
}

static StrategyRecord *ledger_get_or_create(StrategyLedger *sl, const char *sid,
                                             ManufacturingDomain domain, const char *name) {
    StrategyRecord *r = ledger_find(sl, sid);
    if (r) return r;
    if (sl->count >= STRATEGY_LEDGER_MAX) return &sl->records[STRATEGY_LEDGER_MAX - 1];
    r = &sl->records[sl->count++];
    xcpy(r->strategy_id, STRATEGY_ID_LEN, sid);
    r->domain = domain;
    xcpy(r->name, SHORT_LEN, name);
    r->wins = 0;
    r->losses = 0;
    xcpy(r->status, SHORT_LEN, "ACTIVE");
    return r;
}

static void make_sid(char *dst, size_t cap, ManufacturingDomain domain, const char *name) {
    char buf[STRATEGY_ID_LEN];
    snprintf(buf, sizeof buf, "%s:%s", domain_str(domain), name);
    xcpy(dst, cap, buf);
}

void strategy_ledger_init(StrategyLedger *sl) {
    sl->count = 0;
    static const struct { ManufacturingDomain domain; const StrategyDef *defs; int n; } seed[] = {
        {DOM_CAD_MECHANICAL, CAD_MECHANICAL_STRATS, 3},
        {DOM_PCB_ELECTRONICS, PCB_ELECTRONICS_STRATS, 3},
        {DOM_DFM_INJECTION_MOLDING, DFM_INJECTION_MOLDING_STRATS, 3},
        {DOM_FULL_NPD_TURNKEY, FULL_NPD_TURNKEY_STRATS, 2},
    };
    for (size_t s = 0; s < sizeof(seed) / sizeof(seed[0]); s++) {
        for (int i = 0; i < seed[s].n; i++) {
            char sid[STRATEGY_ID_LEN];
            make_sid(sid, sizeof sid, seed[s].domain, seed[s].defs[i].name);
            ledger_get_or_create(sl, sid, seed[s].domain, seed[s].defs[i].name);
        }
    }
}

StrategyChoice strategy_choose(StrategyLedger *sl, ManufacturingDomain domain) {
    int n;
    const StrategyDef *candidates = candidates_for(domain, &n);
    bool explore = rng_next_double(global_rng()) < EXPLORE_RATE;

    int chosen;
    if (explore || n == 1) {
        chosen = (int) rng_choice_index(global_rng(), (size_t) n);
    } else {
        chosen = 0;
        double best = -1.0;
        for (int i = 0; i < n; i++) {
            char sid[STRATEGY_ID_LEN];
            make_sid(sid, sizeof sid, domain, candidates[i].name);
            StrategyRecord *r = ledger_get_or_create(sl, sid, domain, candidates[i].name);
            double wr = strategy_win_rate(r);
            if (wr > best) { best = wr; chosen = i; }
        }
    }

    StrategyChoice choice;
    memset(&choice, 0, sizeof choice);
    xcpy(choice.name, sizeof choice.name, candidates[chosen].name);
    xcpy(choice.blueprint, sizeof choice.blueprint, candidates[chosen].blueprint);
    choice.explore = explore;
    make_sid(choice.strategy_id, sizeof choice.strategy_id, domain, candidates[chosen].name);
    ledger_get_or_create(sl, choice.strategy_id, domain, candidates[chosen].name);
    return choice;
}

void strategy_record_outcome(StrategyLedger *sl, const char *strategy_id, bool won) {
    StrategyRecord *r = ledger_find(sl, strategy_id);
    if (!r) return;
    if (won) r->wins++; else r->losses++;
}
