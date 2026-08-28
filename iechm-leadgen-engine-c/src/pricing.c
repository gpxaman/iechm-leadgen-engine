#include "pricing.h"
#include <math.h>

const double MIN_MARGIN_PCT = 0.35;
static const double DISCOUNT_VS_MARKET = 0.90;
static const double ALUMINUM_USD_PER_KG = 3.15;
static const double ELECTRICITY_USD_PER_KWH = 0.12;

static double round2(double v) { return round(v * 100.0) / 100.0; }

static double base_market_rate(ManufacturingDomain d) {
    switch (d) {
        case DOM_CAD_MECHANICAL: return 1800.0;
        case DOM_PCB_ELECTRONICS: return 2600.0;
        case DOM_ENCLOSURE_DESIGN: return 1500.0;
        case DOM_PROTOTYPING_3D_PRINT: return 900.0;
        case DOM_DFM_INJECTION_MOLDING: return 6500.0;
        case DOM_SHEET_METAL: return 3200.0;
        case DOM_FULL_NPD_TURNKEY: return 12000.0;
        case DOM_UNKNOWN: default: return 1200.0;
    }
}

static double estimate_market_price(ManufacturingDomain domain, bool has_volume, int target_volume,
                                     bool has_budget, double stated_budget_usd) {
    double base = base_market_rate(domain);
    if (has_volume && target_volume > 1) {
        double capped = target_volume > 100000 ? 100000.0 : (double) target_volume;
        double volume_factor = pow(capped, 0.35);
        base += volume_factor * 12.0;
    }
    if (has_budget) {
        base = (base * 0.6) + (stated_budget_usd * 0.4);
    }
    return round2(base);
}

static double estimate_cogs(ManufacturingDomain domain, bool has_volume, int target_volume) {
    int units = has_volume ? target_volume : 1;
    if (units < 1) units = 1;
    bool heavier = (domain == DOM_ENCLOSURE_DESIGN || domain == DOM_PROTOTYPING_3D_PRINT ||
                    domain == DOM_DFM_INJECTION_MOLDING || domain == DOM_SHEET_METAL);
    double est_mass_kg = heavier ? 0.35 : 0.05;
    double material_cost = units * est_mass_kg * ALUMINUM_USD_PER_KG;
    double est_kwh_per_unit = 0.4;
    double power_cost = units * est_kwh_per_unit * ELECTRICITY_USD_PER_KWH;
    int modeled_units = units < 500 ? units : 500;
    double scale = (double) modeled_units / (double) units;
    return round2((material_cost + power_cost) * scale + 40.0);
}

PriceQuote price_quote(ManufacturingDomain domain, bool has_volume, int target_volume,
                        bool has_budget, double stated_budget_usd) {
    PriceQuote q;
    q.market_price_usd = estimate_market_price(domain, has_volume, target_volume, has_budget, stated_budget_usd);
    q.bid_price_usd = round2(q.market_price_usd * DISCOUNT_VS_MARKET);
    q.cogs_usd = estimate_cogs(domain, has_volume, target_volume);
    double floor = q.cogs_usd * (1.0 + MIN_MARGIN_PCT);
    q.floor_violation = q.bid_price_usd < floor;
    if (q.floor_violation) q.bid_price_usd = round2(floor);
    q.margin_pct = q.bid_price_usd != 0.0 ? (q.bid_price_usd - q.cogs_usd) / q.bid_price_usd * 100.0 : 0.0;
    return q;
}
