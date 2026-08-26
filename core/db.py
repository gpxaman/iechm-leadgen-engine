"""
SQLite persistence layer -- the only module that touches storage directly.
Everything else (pipeline, orchestrator, library) works with the
dataclasses in core.models; this module is where they get flattened to
rows and back. Swappable for Postgres/etc. later without touching callers,
since core.library is the only thing that imports this module.
"""
from __future__ import annotations

import json
import sqlite3
from pathlib import Path

from core.models import (
    AgentLayer,
    AgentNode,
    AgentStatus,
    BidRecord,
    ChannelType,
    ClientArchetype,
    LeadStatus,
    ManufacturingDomain,
    QualifiedLead,
    RawSignal,
    SentinelEvent,
    StrategyRecord,
    TrapAnalysis,
)

DB_PATH = Path(__file__).resolve().parent.parent / "iechm.db"

_SCHEMA = """
CREATE TABLE IF NOT EXISTS raw_signals (
    signal_id TEXT PRIMARY KEY,
    channel_type TEXT, platform TEXT, sub_domain TEXT, title TEXT, raw_text TEXT,
    stated_budget_usd REAL, target_volume INTEGER, url TEXT, posted_at TEXT,
    layer0_passed INTEGER, layer0_reason TEXT, day INTEGER
);

CREATE TABLE IF NOT EXISTS leads (
    lead_id TEXT PRIMARY KEY, signal_id TEXT, client_archetype TEXT, domain TEXT,
    cad_software TEXT, materials TEXT, deliverables TEXT, project_stage TEXT,
    target_volume INTEGER, market_price_usd REAL, bid_price_usd REAL, cogs_usd REAL,
    margin_pct REAL, qualification_score REAL, trap_json TEXT, status TEXT, created_at TEXT
);

CREATE TABLE IF NOT EXISTS bids (
    bid_id TEXT PRIMARY KEY, lead_id TEXT, strategy_id TEXT, strategy_name TEXT,
    explore INTEGER, proposal_text TEXT, price_usd REAL, reviewer_status TEXT,
    reviewer_notes TEXT, rewrite_count INTEGER, created_at TEXT
);

CREATE TABLE IF NOT EXISTS agents (
    agent_id TEXT PRIMARY KEY, layer TEXT, role TEXT, channel_type TEXT, sub_domain TEXT,
    status TEXT, model_signature TEXT, drift_score REAL, consecutive_flags INTEGER,
    leads_handled INTEGER, spawned_at TEXT
);

CREATE TABLE IF NOT EXISTS sentinel_events (
    event_id TEXT PRIMARY KEY, agent_id TEXT, event_type TEXT, drift_score REAL,
    reason TEXT, replacement_agent_id TEXT, created_at TEXT
);

CREATE TABLE IF NOT EXISTS strategy_ledger (
    strategy_id TEXT PRIMARY KEY, domain TEXT, name TEXT, wins INTEGER, losses INTEGER, status TEXT
);

CREATE TABLE IF NOT EXISTS run_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT, day INTEGER, channel_type TEXT,
    signals_count INTEGER, layer0_passed INTEGER, qualified INTEGER,
    bids_approved INTEGER, bids_rejected INTEGER, created_at TEXT
);
"""


def connect() -> sqlite3.Connection:
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA foreign_keys = ON")
    return conn


def init_schema(conn: sqlite3.Connection | None = None) -> None:
    owns = conn is None
    conn = conn or connect()
    conn.executescript(_SCHEMA)
    conn.commit()
    if owns:
        conn.close()


def reset(conn: sqlite3.Connection | None = None) -> None:
    owns = conn is None
    conn = conn or connect()
    for table in ("raw_signals", "leads", "bids", "agents", "sentinel_events", "strategy_ledger", "run_log"):
        conn.execute(f"DELETE FROM {table}")
    conn.commit()
    if owns:
        conn.close()


# ---------------------------------------------------------------- writers

def save_signal(conn: sqlite3.Connection, signal: RawSignal, day: int, layer0_passed: bool, layer0_reason: str) -> None:
    conn.execute(
        "INSERT OR REPLACE INTO raw_signals VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)",
        (
            signal.signal_id, signal.channel_type.value, signal.platform, signal.sub_domain,
            signal.title, signal.raw_text, signal.stated_budget_usd, signal.target_volume,
            signal.url, signal.posted_at, int(layer0_passed), layer0_reason, day,
        ),
    )


def save_lead(conn: sqlite3.Connection, lead: QualifiedLead) -> None:
    conn.execute(
        "INSERT OR REPLACE INTO leads VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
        (
            lead.lead_id, lead.signal.signal_id, lead.client_archetype.value, lead.domain.value,
            json.dumps(lead.cad_software), json.dumps(lead.materials), json.dumps(lead.deliverables),
            lead.project_stage, lead.target_volume, lead.market_price_usd, lead.bid_price_usd,
            lead.cogs_usd, lead.margin_pct, lead.qualification_score,
            json.dumps(lead.trap_analysis.to_dict()), lead.status.value, lead.created_at,
        ),
    )


def save_bid(conn: sqlite3.Connection, bid: BidRecord) -> None:
    conn.execute(
        "INSERT OR REPLACE INTO bids VALUES (?,?,?,?,?,?,?,?,?,?,?)",
        (
            bid.bid_id, bid.lead_id, bid.strategy_id, bid.strategy_name, int(bid.explore),
            bid.proposal_text, bid.price_usd, bid.reviewer_status, json.dumps(bid.reviewer_notes),
            bid.rewrite_count, bid.created_at,
        ),
    )


def save_agent(conn: sqlite3.Connection, agent: AgentNode) -> None:
    conn.execute(
        "INSERT OR REPLACE INTO agents VALUES (?,?,?,?,?,?,?,?,?,?,?)",
        (
            agent.agent_id, agent.layer.value, agent.role,
            agent.channel_type.value if agent.channel_type else None, agent.sub_domain,
            agent.status.value, agent.model_signature, agent.drift_score,
            agent.consecutive_flags, agent.leads_handled, agent.spawned_at,
        ),
    )


def save_sentinel_event(conn: sqlite3.Connection, event: SentinelEvent) -> None:
    conn.execute(
        "INSERT OR REPLACE INTO sentinel_events VALUES (?,?,?,?,?,?,?)",
        (
            event.event_id, event.agent_id, event.event_type, event.drift_score,
            event.reason, event.replacement_agent_id, event.created_at,
        ),
    )


def save_strategy(conn: sqlite3.Connection, rec: StrategyRecord) -> None:
    conn.execute(
        "INSERT OR REPLACE INTO strategy_ledger VALUES (?,?,?,?,?,?)",
        (rec.strategy_id, rec.domain.value, rec.name, rec.wins, rec.losses, rec.status),
    )


def save_run_log(conn: sqlite3.Connection, day: int, channel_type: str, signals: int, layer0_passed: int,
                  qualified: int, approved: int, rejected: int, created_at: str) -> None:
    conn.execute(
        "INSERT INTO run_log (day, channel_type, signals_count, layer0_passed, qualified, bids_approved, bids_rejected, created_at) "
        "VALUES (?,?,?,?,?,?,?,?)",
        (day, channel_type, signals, layer0_passed, qualified, approved, rejected, created_at),
    )


# ---------------------------------------------------------------- readers

def get_leads(conn: sqlite3.Connection, limit: int = 100, status: str | None = None, channel_type: str | None = None) -> list[dict]:
    q = """
    SELECT leads.*, raw_signals.platform, raw_signals.channel_type, raw_signals.sub_domain, raw_signals.title AS signal_title
    FROM leads JOIN raw_signals ON leads.signal_id = raw_signals.signal_id
    """
    clauses, params = [], []
    if status:
        clauses.append("leads.status = ?")
        params.append(status)
    if channel_type:
        clauses.append("raw_signals.channel_type = ?")
        params.append(channel_type)
    if clauses:
        q += " WHERE " + " AND ".join(clauses)
    q += " ORDER BY leads.created_at DESC LIMIT ?"
    params.append(limit)
    return [dict(row) for row in conn.execute(q, params).fetchall()]


def get_lead_detail(conn: sqlite3.Connection, lead_id: str) -> dict | None:
    lead_row = conn.execute(
        "SELECT leads.*, raw_signals.* FROM leads JOIN raw_signals ON leads.signal_id = raw_signals.signal_id WHERE lead_id = ?",
        (lead_id,),
    ).fetchone()
    if not lead_row:
        return None
    bid_row = conn.execute("SELECT * FROM bids WHERE lead_id = ? ORDER BY created_at DESC LIMIT 1", (lead_id,)).fetchone()
    return {"lead": dict(lead_row), "bid": dict(bid_row) if bid_row else None}


def get_agents(conn: sqlite3.Connection) -> list[dict]:
    return [dict(row) for row in conn.execute("SELECT * FROM agents ORDER BY layer, channel_type").fetchall()]


def get_sentinel_events(conn: sqlite3.Connection, limit: int = 50) -> list[dict]:
    return [dict(row) for row in conn.execute("SELECT * FROM sentinel_events ORDER BY created_at DESC LIMIT ?", (limit,)).fetchall()]


def get_strategies(conn: sqlite3.Connection) -> list[dict]:
    return [dict(row) for row in conn.execute("SELECT * FROM strategy_ledger ORDER BY domain, name").fetchall()]


def get_funnel(conn: sqlite3.Connection) -> dict:
    totals = conn.execute(
        "SELECT SUM(signals_count) s, SUM(layer0_passed) l0, SUM(qualified) q, SUM(bids_approved) a, SUM(bids_rejected) r FROM run_log"
    ).fetchone()
    by_channel = conn.execute(
        "SELECT channel_type, SUM(signals_count) s, SUM(layer0_passed) l0, SUM(qualified) q, SUM(bids_approved) a "
        "FROM run_log GROUP BY channel_type"
    ).fetchall()
    return {
        "totals": {
            "raw_signals": totals["s"] or 0,
            "layer0_passed": totals["l0"] or 0,
            "qualified": totals["q"] or 0,
            "bids_approved": totals["a"] or 0,
            "bids_rejected": totals["r"] or 0,
        },
        "by_channel": [dict(row) for row in by_channel],
    }
