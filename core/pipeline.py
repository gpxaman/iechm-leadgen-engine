"""
Per-lead pipeline: Layer 0 filter -> Sanitizer -> classify (Layer 1) ->
price -> Strategist -> Writer -> Reviewer (with bounded rewrite loop).

This is the "4-node secure cluster" from the source architecture, wired to
the deterministic filters/pricing/classifier defined elsewhere in core/ so
the whole thing runs without any external LLM call.
"""
from __future__ import annotations

from dataclasses import dataclass

from core import classify, pricing, security
from core.filters import passes_layer0
from core.llm import LLMClient
from core.models import BidRecord, LeadStatus, QualifiedLead, RawSignal, new_id
from core.reviewer import review
from core.strategist import StrategyLedger
from core.writer import draft_proposal

QUALIFICATION_SCORE_FLOOR = 0.35


@dataclass
class PipelineOutcome:
    lead: QualifiedLead | None
    bid: BidRecord | None
    rejected_stage: str | None
    rejected_reason: str | None


def _qualification_score(domain, archetype, trap: security.TrapAnalysis, price: pricing.PriceQuote) -> float:
    score = 0.5
    if domain.value != "unknown":
        score += 0.2
    if archetype.value != "UNKNOWN":
        score += 0.1
    if price.margin_pct >= 40:
        score += 0.15
    elif price.floor_violation:
        score -= 0.25
    if trap.detected_injections:
        score -= 0.05  # not disqualifying, just a small trust discount
    return max(0.0, min(1.0, score))


def run_pipeline(
    signal: RawSignal,
    ledger: StrategyLedger,
    llm: LLMClient | None = None,
    fault_rate: float = 0.02,
) -> PipelineOutcome:
    layer0 = passes_layer0(signal)
    if not layer0.passed:
        return PipelineOutcome(None, None, "LAYER0", layer0.reason)

    trap = security.sanitize(signal.raw_text)
    domain = classify.classify_domain(signal.raw_text)
    archetype = classify.classify_archetype(signal)
    cad = classify.extract_cad_software(signal.raw_text)
    materials = classify.extract_materials(signal.raw_text)
    deliverables = classify.extract_deliverables(signal.raw_text)
    stage = classify.classify_project_stage(signal.raw_text)

    price = pricing.quote(domain, signal.target_volume, signal.stated_budget_usd)
    score = _qualification_score(domain, archetype, trap, price)

    if score < QUALIFICATION_SCORE_FLOOR:
        return PipelineOutcome(None, None, "QUALIFICATION", f"score_{score:.2f}_below_floor")

    lead = QualifiedLead(
        lead_id=new_id("lead"),
        signal=signal,
        client_archetype=archetype,
        domain=domain,
        cad_software=cad,
        materials=materials,
        deliverables=deliverables,
        project_stage=stage,
        target_volume=signal.target_volume,
        market_price_usd=price.market_price_usd,
        bid_price_usd=price.bid_price_usd,
        cogs_usd=price.cogs_usd,
        margin_pct=price.margin_pct,
        qualification_score=score,
        trap_analysis=trap,
        status=LeadStatus.QUALIFIED,
    )

    strategy = ledger.choose(lead)
    rewrite_count = 0
    proposal_text = draft_proposal(lead, strategy, llm, attempt=rewrite_count, fault_rate=fault_rate)
    result = review(lead, proposal_text, rewrite_count)

    while not result.approved and result.needs_rewrite and rewrite_count < 2:
        rewrite_count += 1
        proposal_text = draft_proposal(lead, strategy, llm, attempt=rewrite_count, fault_rate=fault_rate)
        result = review(lead, proposal_text, rewrite_count)

    lead.status = LeadStatus.BID_APPROVED if result.approved else LeadStatus.BID_REJECTED_BY_REVIEWER

    bid = BidRecord(
        bid_id=new_id("bid"),
        lead_id=lead.lead_id,
        strategy_id=strategy.strategy_id,
        strategy_name=strategy.name,
        explore=strategy.explore,
        proposal_text=proposal_text,
        price_usd=lead.bid_price_usd,
        reviewer_status="APPROVED" if result.approved else "REJECTED",
        reviewer_notes=result.notes,
        rewrite_count=rewrite_count,
    )

    return PipelineOutcome(lead, bid, None, None)
