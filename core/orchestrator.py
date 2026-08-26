"""
The Director: runs one full simulated day across every channel adapter,
owns the agent swarm (spawn/deprecate per the 5-lead/day rule), routes
each qualified signal through core.pipeline, and feeds every outcome to
the Sentinel for drift tracking / hot-swap.

This is the orchestration layer described across the conversation this
project is based on, made concrete: Layer 1 (central command) -> Layer 2
(macro channel controllers) -> Layer 3 (platform workers) -> Layer 4
(dynamic sub-domain workers), with a cross-cutting sentinel plane.
"""
from __future__ import annotations

import random
import sqlite3
from collections import defaultdict
from dataclasses import dataclass, field

from core import db
from core.channels import all_adapters
from core.channels.base import ChannelAdapter
from core.filters import passes_layer0
from core.llm import LLMClient
from core.models import (
    AgentLayer,
    AgentNode,
    AgentStatus,
    ChannelType,
    now_iso,
    new_id,
)
from core.pipeline import run_pipeline
from core.sentinel import Sentinel
from core.strategist import StrategyLedger

SUBDOMAIN_SPAWN_THRESHOLD = 5   # qualifying leads/day -> spawn a dedicated L4 worker
SUBDOMAIN_DEPRECATE_THRESHOLD = 2  # below this -> deprecate the L4 worker

_FRICTION_PENALTY = {"LOW": 0.0, "MEDIUM": 0.15, "HIGH": 0.35}

# Per-agent fault bias: most spawned workers stay clean; a small minority
# are drawn as "degrading" (simulating a model/prompt-version drift on that
# one node) so the Reviewer/Sentinel/hot-swap loop has something real to
# catch instead of running forever with a spotless record.
DEGRADING_AGENT_PROBABILITY = 0.08
CLEAN_AGENT_FAULT_RATE = 0.02
DEGRADING_AGENT_FAULT_RATE = 0.45


@dataclass
class ChannelCycleSummary:
    channel_type: str
    platforms: int
    signals: int
    layer0_passed: int
    qualified: int
    bids_approved: int
    bids_rejected: int
    subdomain_agents_spawned: list[str] = field(default_factory=list)
    subdomain_agents_deprecated: list[str] = field(default_factory=list)
    channel_health_score: float = 0.0


@dataclass
class CycleSummary:
    day: int
    channels: list[ChannelCycleSummary]
    sentinel_events: int
    total_signals: int
    total_qualified: int
    total_bids_approved: int


class Orchestrator:
    def __init__(self, llm: LLMClient | None = None):
        self.llm = llm
        self.ledger = StrategyLedger()
        self.sentinel = Sentinel()
        # agent registry keyed by a stable identity string, so re-running
        # a cycle reuses the same agents instead of spawning duplicates
        self.agents: dict[str, AgentNode] = {}
        self.agent_fault_rate: dict[str, float] = {}

    # -- agent registry helpers -------------------------------------------------

    def _assign_fault_bias(self, agent_id: str) -> None:
        r = random.Random(agent_id)
        degrading = r.random() < DEGRADING_AGENT_PROBABILITY
        self.agent_fault_rate[agent_id] = DEGRADING_AGENT_FAULT_RATE if degrading else CLEAN_AGENT_FAULT_RATE

    def _get_or_spawn(self, key: str, layer: AgentLayer, role: str,
                       channel_type: ChannelType | None, sub_domain: str | None) -> AgentNode:
        if key in self.agents and self.agents[key].status != AgentStatus.DEPRECATED:
            return self.agents[key]
        agent = AgentNode(
            agent_id=new_id("agent"),
            layer=layer,
            role=role,
            channel_type=channel_type,
            sub_domain=sub_domain,
            status=AgentStatus.ACTIVE,
            model_signature="primary-extractor-v1",
        )
        self.agents[key] = agent
        self._assign_fault_bias(agent.agent_id)
        return agent

    # -- main cycle ---------------------------------------------------------

    def run_cycle(self, day: int, conn: sqlite3.Connection | None = None) -> CycleSummary:
        owns_conn = conn is None
        conn = conn or db.connect()
        db.init_schema(conn)

        central = self._get_or_spawn("L1:director", AgentLayer.L1_CENTRAL_COMMAND, "Central Director", None, None)
        db.save_agent(conn, central)

        channel_summaries: list[ChannelCycleSummary] = []
        events_before = len(self.sentinel.events)

        for adapter in all_adapters():
            summary = self._run_channel(conn, adapter, day)
            channel_summaries.append(summary)

        for rec in self.ledger.all_records():
            db.save_strategy(conn, rec)

        conn.commit()
        if owns_conn:
            conn.close()

        return CycleSummary(
            day=day,
            channels=channel_summaries,
            sentinel_events=len(self.sentinel.events) - events_before,
            total_signals=sum(c.signals for c in channel_summaries),
            total_qualified=sum(c.qualified for c in channel_summaries),
            total_bids_approved=sum(c.bids_approved for c in channel_summaries),
        )

    def _run_channel(self, conn: sqlite3.Connection, adapter: ChannelAdapter, day: int) -> ChannelCycleSummary:
        ct = adapter.channel_type
        l2_key = f"L2:{ct.value}"
        l2_agent = self._get_or_spawn(l2_key, AgentLayer.L2_MACRO_CHANNEL, f"{ct.value} Channel Controller", ct, None)
        db.save_agent(conn, l2_agent)

        signals = adapter.generate_daily_signals(day)
        subdomain_qualified_counts: dict[str, int] = defaultdict(int)
        subdomain_platform: dict[str, str] = {}

        layer0_passed = qualified = bids_approved = bids_rejected = 0

        for signal in signals:
            platform_key = f"L3:{ct.value}:{signal.platform}"
            worker = self._get_or_spawn(platform_key, AgentLayer.L3_PLATFORM_WORKER, f"{signal.platform} Worker", ct, None)

            responsible_key = platform_key
            if signal.sub_domain:
                sub_key = f"L4:{ct.value}:{signal.platform}:{signal.sub_domain}"
                if sub_key in self.agents and self.agents[sub_key].status == AgentStatus.ACTIVE:
                    responsible_key = sub_key
                    worker = self.agents[sub_key]

            fault_rate = self.agent_fault_rate.get(worker.agent_id, CLEAN_AGENT_FAULT_RATE)
            outcome = run_pipeline(signal, self.ledger, self.llm, fault_rate=fault_rate)

            l0 = passes_layer0(signal)
            db.save_signal(conn, signal, day, l0.passed, l0.reason)

            if outcome.lead is None:
                # nothing to attribute to the worker -- Layer 0 / qualification
                # rejections happen before any agent does meaningful work
                continue

            layer0_passed += 1
            qualified += 1
            if signal.sub_domain:
                subdomain_qualified_counts[f"{signal.platform}::{signal.sub_domain}"] += 1
                subdomain_platform[f"{signal.platform}::{signal.sub_domain}"] = signal.platform

            db.save_lead(conn, outcome.lead)
            if outcome.bid:
                db.save_bid(conn, outcome.bid)
                approved = outcome.bid.reviewer_status == "APPROVED"
                bids_approved += int(approved)
                bids_rejected += int(not approved)
                if approved:
                    self.ledger.record_outcome(outcome.bid.strategy_id, won=True)

                worker.leads_handled += 1
                # anomaly = the Writer needed at least one rewrite (Reviewer
                # caught something) OR never recovered within the rewrite
                # budget -- both are signal that this agent's output needed
                # correcting, which is exactly what the sentinel watches for.
                anomaly = (not approved) or (outcome.bid.rewrite_count > 0)
                if anomaly:
                    reason = f"reviewer_flagged_draft (rewrites={outcome.bid.rewrite_count}, final={outcome.bid.reviewer_status}): " + ",".join(outcome.bid.reviewer_notes)
                else:
                    reason = "ok"
                new_worker, event = self.sentinel.evaluate(worker, anomaly, reason)
                self.agents[responsible_key] = new_worker
                db.save_agent(conn, new_worker)
                if event:
                    db.save_sentinel_event(conn, event)
                    if event.event_type == "HOTSWAP_TRIGGERED":
                        # the quarantined predecessor is also persisted so the
                        # admin panel / UI can show what got pulled and why
                        db.save_agent(conn, worker)
                        self._assign_fault_bias(new_worker.agent_id)
            else:
                db.save_agent(conn, worker)

        spawned, deprecated = self._reconcile_subdomains(conn, ct, subdomain_qualified_counts, subdomain_platform)

        db.save_run_log(
            conn, day, ct.value, len(signals), layer0_passed, qualified,
            bids_approved, bids_rejected, now_iso(),
        )

        friction = adapter.friction_level()
        velocity_score = min(1.0, qualified / 20.0)
        health = round(max(0.0, velocity_score - _FRICTION_PENALTY.get(friction, 0.15)), 3)

        return ChannelCycleSummary(
            channel_type=ct.value,
            platforms=len(adapter.list_platforms()),
            signals=len(signals),
            layer0_passed=layer0_passed,
            qualified=qualified,
            bids_approved=bids_approved,
            bids_rejected=bids_rejected,
            subdomain_agents_spawned=spawned,
            subdomain_agents_deprecated=deprecated,
            channel_health_score=health,
        )

    def _reconcile_subdomains(self, conn: sqlite3.Connection, ct: ChannelType,
                               counts: dict[str, int], platform_of: dict[str, str]) -> tuple[list[str], list[str]]:
        spawned, deprecated = [], []
        for combo, count in counts.items():
            platform, sub_domain = combo.split("::", 1)
            key = f"L4:{ct.value}:{platform}:{sub_domain}"
            existing = self.agents.get(key)

            if count >= SUBDOMAIN_SPAWN_THRESHOLD and (existing is None or existing.status != AgentStatus.ACTIVE):
                agent = AgentNode(
                    agent_id=new_id("agent"), layer=AgentLayer.L4_SUBDOMAIN_WORKER,
                    role=f"{sub_domain} Micro-Worker", channel_type=ct, sub_domain=sub_domain,
                    status=AgentStatus.ACTIVE, model_signature="primary-extractor-v1",
                )
                self.agents[key] = agent
                self._assign_fault_bias(agent.agent_id)
                db.save_agent(conn, agent)
                spawned.append(f"{platform}/{sub_domain}")
            elif existing is not None and existing.status == AgentStatus.ACTIVE and count < SUBDOMAIN_DEPRECATE_THRESHOLD:
                existing.status = AgentStatus.DEPRECATED
                db.save_agent(conn, existing)
                deprecated.append(f"{platform}/{sub_domain}")
        return spawned, deprecated
