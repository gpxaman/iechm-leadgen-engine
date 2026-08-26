"""
Pricing engine.

Implements the pricing rule from the architecture this project is based
on: bid at exactly 10% below an estimated market price, never below a hard
COGS + minimum-margin floor. The floor check is a hard invariant -- the
Reviewer stage (core.reviewer) re-verifies it independently before any bid
can be approved, so a Strategist/Writer bug can never ship an underpriced
bid on its own.
"""
from __future__ import annotations

from dataclasses import dataclass

from core.models import ManufacturingDomain

DISCOUNT_VS_MARKET = 0.90  # bid = 90% of estimated market price
MIN_MARGIN_PCT = 0.35      # never bid below cogs * (1 + this)

ALUMINUM_USD_PER_KG = 3.15
ELECTRICITY_USD_PER_KWH = 0.12

# Rough base-rate table (USD) used to estimate "what a traditional shop
# would charge" for a given domain before volume/complexity adjustment.
# These are illustrative defaults, not a live market feed -- swap in a
# real pricing model / comps API when one exists.
_BASE_MARKET_RATE = {
    ManufacturingDomain.CAD_MECHANICAL: 1800.0,
    ManufacturingDomain.PCB_ELECTRONICS: 2600.0,
    ManufacturingDomain.ENCLOSURE_DESIGN: 1500.0,
    ManufacturingDomain.PROTOTYPING_3D_PRINT: 900.0,
    ManufacturingDomain.DFM_INJECTION_MOLDING: 6500.0,
    ManufacturingDomain.SHEET_METAL: 3200.0,
    ManufacturingDomain.FULL_NPD_TURNKEY: 12000.0,
    ManufacturingDomain.UNKNOWN: 1200.0,
}


@dataclass
class PriceQuote:
    market_price_usd: float
    bid_price_usd: float
    cogs_usd: float
    margin_pct: float
    floor_violation: bool


def estimate_market_price(domain: ManufacturingDomain, target_volume: int | None, stated_budget_usd: float | None) -> float:
    base = _BASE_MARKET_RATE.get(domain, 1200.0)
    if target_volume and target_volume > 1:
        # volume adds tooling/setup amortization, with diminishing per-unit weight
        volume_factor = min(target_volume, 100_000) ** 0.35
        base += volume_factor * 12.0
    if stated_budget_usd:
        # anchor loosely to what the client already signaled, without
        # letting a lowball ask collapse the estimate entirely
        base = (base * 0.6) + (stated_budget_usd * 0.4)
    return round(base, 2)


def estimate_cogs(domain: ManufacturingDomain, target_volume: int | None) -> float:
    units = max(target_volume or 1, 1)
    est_mass_kg = 0.35 if domain in (
        ManufacturingDomain.ENCLOSURE_DESIGN,
        ManufacturingDomain.PROTOTYPING_3D_PRINT,
        ManufacturingDomain.DFM_INJECTION_MOLDING,
        ManufacturingDomain.SHEET_METAL,
    ) else 0.05
    material_cost = units * est_mass_kg * ALUMINUM_USD_PER_KG
    est_kwh_per_unit = 0.4
    power_cost = units * est_kwh_per_unit * ELECTRICITY_USD_PER_KWH
    # cap the modeled run to a representative production batch so a
    # 100k-unit RFQ doesn't dominate the single-lead cost estimate
    modeled_units = min(units, 500)
    scale = modeled_units / units
    return round((material_cost + power_cost) * scale + 40.0, 2)  # +40 flat engineering setup


def quote(domain: ManufacturingDomain, target_volume: int | None, stated_budget_usd: float | None) -> PriceQuote:
    market = estimate_market_price(domain, target_volume, stated_budget_usd)
    bid = round(market * DISCOUNT_VS_MARKET, 2)
    cogs = estimate_cogs(domain, target_volume)
    floor = cogs * (1 + MIN_MARGIN_PCT)
    floor_violation = bid < floor
    if floor_violation:
        bid = round(floor, 2)
    margin_pct = ((bid - cogs) / bid * 100.0) if bid else 0.0
    return PriceQuote(
        market_price_usd=market,
        bid_price_usd=bid,
        cogs_usd=cogs,
        margin_pct=margin_pct,
        floor_violation=floor_violation,
    )
