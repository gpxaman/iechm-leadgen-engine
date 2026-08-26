"""
The Strategist -- picks a bidding blueprint per lead using an
explore/exploit policy over a per-domain strategy ledger, then hands the
Writer a concrete angle instead of a blank page.
"""
from __future__ import annotations

import random
from dataclasses import dataclass

from core.models import ManufacturingDomain, QualifiedLead, StrategyRecord

EXPLORE_RATE = 0.20

_DEFAULT_STRATEGIES: dict[ManufacturingDomain, list[tuple[str, str]]] = {
    ManufacturingDomain.CAD_MECHANICAL: [
        ("technical_breakdown", "Lead with a detailed technical breakdown and tolerance callouts"),
        ("speed_first", "Lead with guaranteed turnaround time and immediate start"),
        ("challenge_premise", "Politely challenge an assumption in the brief to demonstrate expertise"),
    ],
    ManufacturingDomain.PCB_ELECTRONICS: [
        ("dfm_audit_hook", "Open with a free rapid DFM/EMC risk audit of their stated spec"),
        ("speed_first", "Lead with guaranteed turnaround time and immediate start"),
        ("portfolio_proof", "Lead with directly comparable past PCB layout outcomes"),
    ],
    ManufacturingDomain.DFM_INJECTION_MOLDING: [
        ("dfm_audit_hook", "Open with a free rapid DFM/EMC risk audit of their stated spec"),
        ("cost_down", "Lead with unit-cost reduction (VAVE) and tooling-cycle optimization"),
        ("zero_tooling", "Lead with zero-tooling-cost monolithic manufacturing capability"),
    ],
    ManufacturingDomain.FULL_NPD_TURNKEY: [
        ("derisk_turnkey", "Sell de-risking: full concept-to-production ownership in one contract"),
        ("technical_breakdown", "Lead with a detailed technical breakdown and tolerance callouts"),
    ],
}
_FALLBACK_STRATEGIES = [
    ("technical_breakdown", "Lead with a detailed technical breakdown and tolerance callouts"),
    ("speed_first", "Lead with guaranteed turnaround time and immediate start"),
    ("cost_down", "Lead with unit-cost reduction and quality assurance"),
]


@dataclass
class StrategyChoice:
    strategy_id: str
    name: str
    blueprint: str
    explore: bool


class StrategyLedger:
    """In-memory + externally-synced win/loss ledger. core.db persists the
    same records; this class owns the selection policy."""

    def __init__(self):
        self._records: dict[str, StrategyRecord] = {}
        for domain, strategies in _DEFAULT_STRATEGIES.items():
            for name, _ in strategies:
                sid = f"{domain.value}:{name}"
                self._records[sid] = StrategyRecord(sid, domain, name, wins=0, losses=0, status="ACTIVE")

    def _candidates(self, domain: ManufacturingDomain) -> list[tuple[str, str]]:
        return _DEFAULT_STRATEGIES.get(domain, _FALLBACK_STRATEGIES)

    def choose(self, lead: QualifiedLead) -> StrategyChoice:
        candidates = self._candidates(lead.domain)
        explore = random.random() < EXPLORE_RATE

        if explore or len(candidates) == 1:
            name, blueprint = random.choice(candidates)
        else:
            def rec_for(name: str) -> StrategyRecord:
                sid = f"{lead.domain.value}:{name}"
                return self._records.setdefault(sid, StrategyRecord(sid, lead.domain, name, 0, 0, "ACTIVE"))

            best = max(candidates, key=lambda nb: rec_for(nb[0]).win_rate)
            name, blueprint = best

        sid = f"{lead.domain.value}:{name}"
        self._records.setdefault(sid, StrategyRecord(sid, lead.domain, name, 0, 0, "ACTIVE"))
        return StrategyChoice(strategy_id=sid, name=name, blueprint=blueprint, explore=explore)

    def record_outcome(self, strategy_id: str, won: bool) -> None:
        rec = self._records.get(strategy_id)
        if not rec:
            return
        if won:
            rec.wins += 1
        else:
            rec.losses += 1

    def all_records(self) -> list[StrategyRecord]:
        return list(self._records.values())
