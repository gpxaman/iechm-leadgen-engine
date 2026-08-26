"""
core.library -- THE public API of this system.

Every caller outside of core/ itself (the REST server, the CLI demo,
tests, a notebook, whatever) should only ever import from here. It is the
seam that makes the UI replaceable: core.library has no idea a web server
or a browser exists, it just exposes plain functions that take/return
JSON-friendly dicts and primitives.

Holds one process-wide Orchestrator so agent state (the swarm, the
strategy ledger, the sentinel's drift history) persists across calls
within a run, the same way a real long-lived service would.
"""
from __future__ import annotations

from dataclasses import asdict

from core import db
from core.channels import REGISTRY
from core.orchestrator import Orchestrator

_ORCH: Orchestrator | None = None
_CURRENT_DAY = 0


def _orch() -> Orchestrator:
    global _ORCH
    if _ORCH is None:
        _ORCH = Orchestrator()
    return _ORCH


def init() -> None:
    db.init_schema()


def reset_all() -> None:
    global _ORCH, _CURRENT_DAY
    db.init_schema()
    db.reset()
    _ORCH = Orchestrator()
    _CURRENT_DAY = 0


def run_cycle(day: int | None = None) -> dict:
    """Run one simulated day across every channel. If `day` is omitted,
    advances the internal day counter automatically (what the UI's "Run
    Next Cycle" button calls)."""
    global _CURRENT_DAY
    init()
    if day is None:
        _CURRENT_DAY += 1
        day = _CURRENT_DAY
    else:
        _CURRENT_DAY = max(_CURRENT_DAY, day)

    summary = _orch().run_cycle(day)
    return {
        "day": summary.day,
        "total_signals": summary.total_signals,
        "total_qualified": summary.total_qualified,
        "total_bids_approved": summary.total_bids_approved,
        "sentinel_events_this_cycle": summary.sentinel_events,
        "channels": [asdict(c) for c in summary.channels],
    }


def current_day() -> int:
    return _CURRENT_DAY


def list_leads(limit: int = 100, status: str | None = None, channel_type: str | None = None) -> list[dict]:
    conn = db.connect()
    try:
        return db.get_leads(conn, limit=limit, status=status, channel_type=channel_type)
    finally:
        conn.close()


def get_lead(lead_id: str) -> dict | None:
    conn = db.connect()
    try:
        return db.get_lead_detail(conn, lead_id)
    finally:
        conn.close()


def list_agents() -> list[dict]:
    conn = db.connect()
    try:
        return db.get_agents(conn)
    finally:
        conn.close()


def list_sentinel_events(limit: int = 50) -> list[dict]:
    conn = db.connect()
    try:
        return db.get_sentinel_events(conn, limit=limit)
    finally:
        conn.close()


def list_strategies() -> list[dict]:
    conn = db.connect()
    try:
        return db.get_strategies(conn)
    finally:
        conn.close()


def funnel_metrics() -> dict:
    conn = db.connect()
    try:
        return db.get_funnel(conn)
    finally:
        conn.close()


def list_channel_types() -> list[dict]:
    out = []
    for ct, adapter in REGISTRY.items():
        platforms = adapter.list_platforms()
        out.append({
            "channel_type": ct.value,
            "platforms": platforms,
            "sub_domains": {p: adapter.sub_domains(p) for p in platforms if adapter.sub_domains(p)},
            "friction_level": adapter.friction_level(),
        })
    return out
