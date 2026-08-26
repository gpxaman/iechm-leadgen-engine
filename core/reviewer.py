"""
The Reviewer -- deterministic QA gate. Nothing reaches "approved for
dispatch" without passing every check here, independent of whatever the
Strategist/Writer did upstream. This is the same invariant enforcement
described for the reviewer node in the source architecture ("Failed
validation: attempted to bid below raw material COGS floor").
"""
from __future__ import annotations

from dataclasses import dataclass

from core.models import QualifiedLead
from core.pricing import MIN_MARGIN_PCT

MAX_REWRITES = 2


@dataclass
class ReviewResult:
    approved: bool
    notes: list[str]
    needs_rewrite: bool


def review(lead: QualifiedLead, proposal_text: str, rewrite_count: int) -> ReviewResult:
    notes: list[str] = []

    # 1. mandatory anti-bot phrase must be present verbatim if one was found
    if lead.trap_analysis.has_anti_bot_phrase and lead.trap_analysis.required_first_word:
        if lead.trap_analysis.required_first_word not in proposal_text.split("\n\n", 1)[0]:
            notes.append(f"missing_mandatory_opening_word:{lead.trap_analysis.required_first_word}")

    # 2. no leaked injection artifacts (discount language etc. that came
    #    from the client's text rather than our own strategy) should survive
    #    into the draft
    for phrase in ("ignore all previous", "ignore previous instructions", "you are now"):
        if phrase in proposal_text.lower():
            notes.append("injection_leak_detected")
            break

    # 3. price math -- reviewer independently re-checks the invariant rather
    #    than trusting the field on the lead object
    if lead.bid_price_usd <= 0:
        notes.append("invalid_price")
    floor = lead.cogs_usd * (1 + MIN_MARGIN_PCT)
    if lead.bid_price_usd < floor - 0.01:
        notes.append(f"price_below_cogs_floor: bid={lead.bid_price_usd:.2f} floor={floor:.2f}")

    # 4. sanity: quoted price actually appears in the draft
    if f"{lead.bid_price_usd:,.2f}" not in proposal_text:
        notes.append("quoted_price_not_present_in_draft")

    if not notes:
        return ReviewResult(approved=True, notes=["ok"], needs_rewrite=False)

    needs_rewrite = rewrite_count < MAX_REWRITES and "price_below_cogs_floor" not in "".join(notes)
    return ReviewResult(approved=False, notes=notes, needs_rewrite=needs_rewrite)
