"""
Layer 0 -- deterministic pre-filter.

This runs BEFORE any LLM/agent ever sees a raw signal. It is plain string
matching and arithmetic on purpose: at real scale (per the architecture
discussion this project grew out of) the raw firehose is orders of
magnitude larger than what's worth spending model tokens on, so the first
cut has to be free. Only what survives here reaches core.security /
core.classify.
"""
from __future__ import annotations

import re
from dataclasses import dataclass

from core.models import RawSignal

# Categories IECHM does not manufacture -- instant reject regardless of
# anything else in the post.
BLACKLIST_TERMS = [
    r"\bfabric\b", r"\btextile", r"\bapparel\b", r"\bgarment", r"\bclothing\b",
    r"\bfood\b", r"\bagricultur", r"\bwheat\b", r"\bfertiliz",
    r"\bchemical formulation\b", r"\bpharmaceutical\b", r"\bcosmetic",
    r"\bcontent writ", r"\bcopywrit", r"\bseo\b", r"\bwordpress\b",
    r"\bmobile app\b", r"\bwebsite design\b", r"\blogo design only\b",
]

# Positive signal terms -- physical hardware / NPD markers. A listing needs
# at least one hit to be worth classifying.
POSITIVE_TERMS = [
    r"\bcad\b", r"\bstep\b", r"\bstl\b", r"\biges\b", r"\bpcb\b", r"\bgerber\b",
    r"\baltium\b", r"\bkicad\b", r"\bfusion ?360\b", r"\bsolidworks\b",
    r"\bcnc\b", r"\binjection mold", r"\bsheet metal\b", r"\bdfm\b", r"\bdfa\b",
    r"\bbom\b", r"\benclosure\b", r"\b3d print", r"\bprototyp", r"\btooling\b",
    r"\baluminum\b", r"\baluminium\b", r"\bmechanical design\b", r"\bfirmware\b",
    r"\bembedded\b", r"\brfq\b", r"\bmoq\b", r"\bmanufactur",
]

_BLACKLIST_RE = re.compile("|".join(BLACKLIST_TERMS), re.IGNORECASE)
_POSITIVE_RE = re.compile("|".join(POSITIVE_TERMS), re.IGNORECASE)

MIN_VIABLE_BUDGET_USD = 40.0  # below this, not worth a bid even for a quick DFM note


@dataclass
class Layer0Result:
    passed: bool
    reason: str


def passes_layer0(signal: RawSignal) -> Layer0Result:
    text = f"{signal.title}\n{signal.raw_text}"

    if _BLACKLIST_RE.search(text):
        return Layer0Result(False, "blacklisted_category")

    if not _POSITIVE_RE.search(text):
        return Layer0Result(False, "no_hardware_markers")

    if signal.stated_budget_usd is not None:
        if signal.stated_budget_usd < MIN_VIABLE_BUDGET_USD:
            return Layer0Result(False, "budget_below_floor")

        if signal.target_volume and signal.target_volume > 0:
            per_unit = signal.stated_budget_usd / signal.target_volume
            # A per-unit ask under 2 cents for anything requiring tooling/CAD
            # is a mathematically impossible RFQ (per the trade-directory
            # spam pattern this rule exists to catch).
            if per_unit < 0.02 and signal.target_volume >= 1000:
                return Layer0Result(False, "budget_mathematically_impossible")

    return Layer0Result(True, "ok")
