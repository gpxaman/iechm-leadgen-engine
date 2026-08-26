#!/usr/bin/env python3
"""
CLI demo -- runs a few simulated days through the whole pipeline and prints
a summary. Zero external dependencies (stdlib only). Good smoke test after
touching core/, and a quick way to see the system work without the UI.

Usage:
    python3 run_demo.py [num_days]
"""
from __future__ import annotations

import sys

from core import library


def main() -> None:
    days = int(sys.argv[1]) if len(sys.argv) > 1 else 3
    library.reset_all()

    print(f"IECHM Lead Generation Engine -- running {days} simulated day(s)\n")

    for _ in range(days):
        summary = library.run_cycle()
        print(f"=== Day {summary['day']} ===")
        print(f"  raw signals ingested : {summary['total_signals']}")
        print(f"  qualified leads      : {summary['total_qualified']}")
        print(f"  bids approved        : {summary['total_bids_approved']}")
        print(f"  sentinel events      : {summary['sentinel_events_this_cycle']}")
        for ch in summary["channels"]:
            print(
                f"    - {ch['channel_type']:<24} signals={ch['signals']:<4} "
                f"qualified={ch['qualified']:<4} bids_ok={ch['bids_approved']:<3} "
                f"health={ch['channel_health_score']}"
            )
            if ch["subdomain_agents_spawned"]:
                print(f"        spawned L4 workers: {ch['subdomain_agents_spawned']}")
            if ch["subdomain_agents_deprecated"]:
                print(f"        deprecated L4 workers: {ch['subdomain_agents_deprecated']}")
        print()

    print("=== Funnel (cumulative) ===")
    funnel = library.funnel_metrics()
    for k, v in funnel["totals"].items():
        print(f"  {k:<16}: {v}")

    print("\n=== Agent swarm ===")
    agents = library.list_agents()
    by_layer: dict[str, int] = {}
    for a in agents:
        by_layer[a["layer"]] = by_layer.get(a["layer"], 0) + 1
    for layer, count in sorted(by_layer.items()):
        print(f"  {layer:<24}: {count}")
    print(f"  TOTAL AGENTS: {len(agents)}")

    events = library.list_sentinel_events(limit=10)
    print(f"\n=== Sentinel events (most recent {len(events)}) ===")
    for e in events:
        print(f"  [{e['event_type']}] agent={e['agent_id']} drift={e['drift_score']:.2f} :: {e['reason']}")

    print("\n=== Strategy ledger ===")
    for s in library.list_strategies():
        print(f"  {s['strategy_id']:<45} wins={s['wins']:<3} losses={s['losses']:<3} status={s['status']}")

    print("\n=== Sample qualified lead (full pipeline trace) ===")
    leads = library.list_leads(limit=1, status="BID_APPROVED")
    if leads:
        detail = library.get_lead(leads[0]["lead_id"])
        lead = detail["lead"]
        bid = detail["bid"]
        print(f"  Platform     : {lead['platform']} / {lead['sub_domain'] or '-'}")
        print(f"  Title        : {lead['title']}")
        print(f"  Archetype    : {lead['client_archetype']}")
        print(f"  Domain       : {lead['domain']}")
        print(f"  Market price : ${lead['market_price_usd']:,.2f}")
        print(f"  Bid price    : ${lead['bid_price_usd']:,.2f}  (margin {lead['margin_pct']:.1f}%)")
        if bid:
            print(f"  Strategy     : {bid['strategy_name']} (explore={bool(bid['explore'])})")
            print(f"  Reviewer     : {bid['reviewer_status']}")
            print(f"  --- proposal ---\n{bid['proposal_text']}\n")

    print(f"\nDB written to: core/db.py -> {library.db.DB_PATH}")
    print("Start the dashboard with:  python3 server.py")


if __name__ == "__main__":
    main()
