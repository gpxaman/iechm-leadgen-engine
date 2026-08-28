/* Pricing engine -- mirrors core/pricing.py. Bid at 10% below estimated
 * market price, never below a hard COGS + 35%-margin floor. */
#ifndef IECHM_PRICING_H
#define IECHM_PRICING_H

#include "models.h"

extern const double MIN_MARGIN_PCT; /* 0.35, re-checked independently by the reviewer */

typedef struct {
    double market_price_usd;
    double bid_price_usd;
    double cogs_usd;
    double margin_pct;
    bool floor_violation;
} PriceQuote;

PriceQuote price_quote(ManufacturingDomain domain, bool has_volume, int target_volume,
                        bool has_budget, double stated_budget_usd);

#endif
