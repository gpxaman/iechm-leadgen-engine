"""
Core data model for the IECHM lead-generation engine.

Everything downstream (channels, pipeline, orchestrator, API server, UI)
speaks these types. Kept as stdlib dataclasses/enums on purpose -- no
pydantic/fastapi dependency, so this runs anywhere with a bare Python 3.9+
interpreter and can be swapped for a pydantic-backed version later without
touching callers (they only ever import from core.library).
"""
from __future__ import annotations

import uuid
from dataclasses import dataclass, field, asdict
from datetime import datetime, timezone
from enum import Enum
from typing import Optional


def now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


def new_id(prefix: str) -> str:
    return f"{prefix}_{uuid.uuid4().hex[:10]}"


class ChannelType(str, Enum):
    FREELANCE = "FREELANCE_MARKETPLACE"
    B2B_TRADE = "B2B_TRADE_DIRECTORY"
    COMMUNITY = "COMMUNITY_FORUM"
    BROKERAGE = "AGENCY_BROKERAGE"
    OUTBOUND = "OUTBOUND_SIGNAL"


class ClientArchetype(str, Enum):
    NPD_INNOVATOR = "NPD_INNOVATOR"
    MIDDLEMAN_RESELLER = "MIDDLEMAN_RESELLER_OEM_ODM"
    SME_ENGINEERING = "SME_ENGINEERING_OVERFLOW"
    CROWDFUNDER = "CROWDFUNDER_FUNDED"
    INSTITUTIONAL = "INSTITUTIONAL_PUBLIC_SECTOR"
    UNKNOWN = "UNKNOWN"


class ManufacturingDomain(str, Enum):
    CAD_MECHANICAL = "cad_mechanical"
    PCB_ELECTRONICS = "pcb_electronics"
    ENCLOSURE_DESIGN = "enclosure_design"
    PROTOTYPING_3D_PRINT = "prototyping_3d_print"
    DFM_INJECTION_MOLDING = "dfm_injection_molding"
    SHEET_METAL = "sheet_metal"
    FULL_NPD_TURNKEY = "full_npd_turnkey"
    UNKNOWN = "unknown"


class LeadStatus(str, Enum):
    REJECTED_LAYER0 = "REJECTED_LAYER0"
    REJECTED_QUALIFICATION = "REJECTED_QUALIFICATION"
    QUALIFIED = "QUALIFIED"
    BID_DRAFTED = "BID_DRAFTED"
    BID_APPROVED = "BID_APPROVED"
    BID_REJECTED_BY_REVIEWER = "BID_REJECTED_BY_REVIEWER"


class AgentStatus(str, Enum):
    ACTIVE = "ACTIVE"
    IDLE = "IDLE"
    QUARANTINED = "QUARANTINED"
    DEPRECATED = "DEPRECATED"


class AgentLayer(str, Enum):
    L1_CENTRAL_COMMAND = "L1_CENTRAL_COMMAND"
    L2_MACRO_CHANNEL = "L2_MACRO_CHANNEL"
    L3_PLATFORM_WORKER = "L3_PLATFORM_WORKER"
    L4_SUBDOMAIN_WORKER = "L4_SUBDOMAIN_WORKER"
    SENTINEL = "SENTINEL"


@dataclass
class TrapAnalysis:
    """Output of the Sanitizer stage."""
    has_anti_bot_phrase: bool = False
    required_first_word: Optional[str] = None
    detected_injections: list[str] = field(default_factory=list)
    math_verification: Optional[str] = None
    sanitized_text: str = ""

    def to_dict(self) -> dict:
        return asdict(self)


@dataclass
class RawSignal:
    """A single unstructured listing/RFQ/post as it comes off a channel adapter."""
    signal_id: str
    channel_type: ChannelType
    platform: str
    sub_domain: str
    title: str
    raw_text: str
    stated_budget_usd: Optional[float]
    target_volume: Optional[int]
    url: str
    posted_at: str = field(default_factory=now_iso)

    def to_dict(self) -> dict:
        d = asdict(self)
        d["channel_type"] = self.channel_type.value
        return d


@dataclass
class QualifiedLead:
    """A RawSignal that survived Layer 0 + classification (Layer 1)."""
    lead_id: str
    signal: RawSignal
    client_archetype: ClientArchetype
    domain: ManufacturingDomain
    cad_software: list[str]
    materials: list[str]
    deliverables: list[str]
    project_stage: str
    target_volume: Optional[int]
    market_price_usd: float
    bid_price_usd: float
    cogs_usd: float
    margin_pct: float
    qualification_score: float
    trap_analysis: TrapAnalysis
    status: LeadStatus = LeadStatus.QUALIFIED
    created_at: str = field(default_factory=now_iso)

    def to_dict(self) -> dict:
        return {
            "lead_id": self.lead_id,
            "signal": self.signal.to_dict(),
            "client_archetype": self.client_archetype.value,
            "domain": self.domain.value,
            "cad_software": self.cad_software,
            "materials": self.materials,
            "deliverables": self.deliverables,
            "project_stage": self.project_stage,
            "target_volume": self.target_volume,
            "market_price_usd": round(self.market_price_usd, 2),
            "bid_price_usd": round(self.bid_price_usd, 2),
            "cogs_usd": round(self.cogs_usd, 2),
            "margin_pct": round(self.margin_pct, 2),
            "qualification_score": round(self.qualification_score, 3),
            "trap_analysis": self.trap_analysis.to_dict(),
            "status": self.status.value,
            "created_at": self.created_at,
        }


@dataclass
class BidRecord:
    """Output of the Strategist -> Writer -> Reviewer proposal pipeline."""
    bid_id: str
    lead_id: str
    strategy_id: str
    strategy_name: str
    explore: bool
    proposal_text: str
    price_usd: float
    reviewer_status: str  # APPROVED | REJECTED | REWRITE_REQUESTED
    reviewer_notes: list[str]
    rewrite_count: int = 0
    created_at: str = field(default_factory=now_iso)

    def to_dict(self) -> dict:
        return asdict(self)


@dataclass
class AgentNode:
    agent_id: str
    layer: AgentLayer
    role: str
    channel_type: Optional[ChannelType]
    sub_domain: Optional[str]
    status: AgentStatus
    model_signature: str
    drift_score: float = 0.0
    consecutive_flags: int = 0
    leads_handled: int = 0
    spawned_at: str = field(default_factory=now_iso)

    def to_dict(self) -> dict:
        d = asdict(self)
        d["layer"] = self.layer.value
        d["status"] = self.status.value
        d["channel_type"] = self.channel_type.value if self.channel_type else None
        return d


@dataclass
class SentinelEvent:
    event_id: str
    agent_id: str
    event_type: str  # DRIFT_FLAG | HOTSWAP_TRIGGERED | RESUMED
    drift_score: float
    reason: str
    replacement_agent_id: Optional[str] = None
    created_at: str = field(default_factory=now_iso)

    def to_dict(self) -> dict:
        return asdict(self)


@dataclass
class StrategyRecord:
    strategy_id: str
    domain: ManufacturingDomain
    name: str
    wins: int
    losses: int
    status: str  # ACTIVE | EXPLORING | PLANNED | DEPRECATED

    @property
    def win_rate(self) -> float:
        total = self.wins + self.losses
        return (self.wins / total) if total else 0.0

    def to_dict(self) -> dict:
        d = asdict(self)
        d["domain"] = self.domain.value
        d["win_rate"] = round(self.win_rate, 3)
        return d
