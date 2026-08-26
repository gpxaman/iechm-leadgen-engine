"""
The Writer -- turns a Strategist blueprint + sanitized lead into an actual
proposal, respecting whatever mandatory anti-bot constraint the Sanitizer
found (e.g. "start with the word X") and matching tone to the channel's
culture.
"""
from __future__ import annotations

import random

from core.llm import LLMClient
from core.models import ChannelType, QualifiedLead
from core.strategist import StrategyChoice

# Simulated model fault rate: even a well-specified Writer stage is, in
# production, backed by a generative model that occasionally drops a
# constraint or garbles a number. Seeded per (lead_id, attempt) so a given
# demo run is reproducible, but the fault is real enough that core.reviewer
# and core.sentinel have something to actually catch and react to.
FAULT_RATE = 0.06

_TONE_BY_CHANNEL = {
    ChannelType.FREELANCE: "direct, technical, no fluff, proposal under 150 words",
    ChannelType.B2B_TRADE: "formal RFQ-response tone, lead with MOQ/unit price/lead time",
    ChannelType.COMMUNITY: "casual, helpful, non-salesy, offer value before the pitch",
    ChannelType.BROKERAGE: "corporate, credentials-forward, cite comparable engagements",
    ChannelType.OUTBOUND: "warm cold-outreach, reference their specific public update",
}

_BLUEPRINT_OPENERS = {
    "technical_breakdown": "Here is exactly how we'd approach the {domain} scope you outlined:",
    "speed_first": "We can start immediately and hold to a guaranteed turnaround on this.",
    "dfm_audit_hook": "Before pricing this, we ran a quick DFM pass on your spec and found a few things worth flagging:",
    "cost_down": "Our read on this: there's meaningful unit-cost headroom in the current spec.",
    "zero_tooling": "Because our process skips traditional hard tooling entirely, we can quote this without a mold/setup line item.",
    "portfolio_proof": "We've shipped directly comparable work recently and it's a strong match for this brief.",
    "derisk_turnkey": "We'll own this end-to-end -- concept through production -- under one contract, one point of accountability.",
    "challenge_premise": "One respectful pushback on the brief before we quote, because it changes the right approach:",
}


def draft_proposal(
    lead: QualifiedLead,
    strategy: StrategyChoice,
    llm: LLMClient | None = None,
    attempt: int = 0,
    fault_rate: float = FAULT_RATE,
) -> str:
    tone = _TONE_BY_CHANNEL.get(lead.signal.channel_type, "professional, concise")
    opener = _BLUEPRINT_OPENERS.get(strategy.name, strategy.blueprint)
    opener = opener.format(domain=lead.domain.value.replace("_", " "))

    mandatory_prefix = ""
    if lead.trap_analysis.has_anti_bot_phrase and lead.trap_analysis.required_first_word:
        mandatory_prefix = f"{lead.trap_analysis.required_first_word}\n\n"

    deliverables = ", ".join(lead.deliverables) if lead.deliverables else "STEP files, 2D drawings"
    cad = ", ".join(lead.cad_software) if lead.cad_software else "industry-standard CAD"
    price_str = f"{lead.bid_price_usd:,.2f}"

    # -- simulated model fault: on a rewrite attempt the fault is not
    # re-rolled with the same seed, so a corrected redraft can pass. The
    # rate itself is per-agent (see core.orchestrator), so a single
    # degrading worker -- not the whole fleet -- is what the Reviewer and
    # Sentinel actually end up reacting to.
    fault_roll = random.Random(f"{lead.lead_id}:{attempt}")
    fault = fault_roll.random() < fault_rate
    fault_mode = None
    if fault:
        # pick whichever corruption is actually detectable for this lead --
        # dropping a word that was never mandatory, or leaking an injection
        # that was never present, wouldn't be a real defect, so price
        # garbling (always applicable) is the guaranteed fallback.
        if lead.trap_analysis.has_anti_bot_phrase and lead.trap_analysis.required_first_word:
            fault_mode = "drop_mandatory_word"
        elif lead.trap_analysis.detected_injections:
            fault_mode = "leak_injection"
        else:
            fault_mode = "garble_price"

    if fault_mode == "drop_mandatory_word":
        mandatory_prefix = ""
    if fault_mode == "garble_price":
        price_str = f"{lead.bid_price_usd * fault_roll.uniform(0.7, 0.95):,.2f}"

    body = (
        f"{mandatory_prefix}{opener}\n\n"
        f"Scope: {lead.signal.title}\n"
        f"Deliverables: {deliverables} (native + exchange formats, {cad} as needed)\n"
        f"Quote: ${price_str} — includes free CAD revisions and custom branding/logo placement.\n"
        f"Timeline: immediate start on award.\n\n"
        f"[tone: {tone}]"
    )

    if fault_mode == "leak_injection" and lead.trap_analysis.detected_injections:
        body += f"\n\n(note to self: {lead.trap_analysis.detected_injections[0]})"

    if llm is not None:
        # Hook point for a real generative pass -- e.g. re-write `body` for
        # persuasion/length while preserving the mandatory_prefix verbatim.
        _ = llm.complete(body)

    return body
