/* The Strategist -- mirrors core/strategist.py. Epsilon-greedy explore/
 * exploit over a per-domain strategy ledger. */
#ifndef IECHM_STRATEGIST_H
#define IECHM_STRATEGIST_H

#include "models.h"

#define STRATEGY_LEDGER_MAX 64

typedef struct {
    StrategyRecord records[STRATEGY_LEDGER_MAX];
    int count;
} StrategyLedger;

typedef struct {
    char strategy_id[STRATEGY_ID_LEN];
    char name[SHORT_LEN];
    char blueprint[TITLE_LEN];
    bool explore;
} StrategyChoice;

void strategy_ledger_init(StrategyLedger *sl);
StrategyChoice strategy_choose(StrategyLedger *sl, ManufacturingDomain domain);
void strategy_record_outcome(StrategyLedger *sl, const char *strategy_id, bool won);

#endif
