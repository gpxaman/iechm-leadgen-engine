# IECHM Lead Generation Engine

A working implementation of the autonomous lead-generation architecture from
the design conversation: multiple lead **channel types** (freelance
marketplaces, B2B trade directories, community forums, agency brokerages,
outbound signals) feeding a **Layer 0 -> Sanitizer -> Strategist -> Writer ->
Reviewer** agent pipeline, orchestrated by a **Director** that dynamically
spawns/deprecates sub-domain workers and a **Sentinel** that watches for
drifting agents and hot-swaps them.

It runs with **zero external dependencies** (pure Python 3.10+ stdlib — no
pip install needed) so it works in this environment as-is. Every "agent" and
"LLM call" has a deterministic, rule-based default implementation, with a
single pluggable seam (`core/llm.py`) to swap in a real model later.

## Why it's structured this way

The one hard requirement driving the layout: **the UI has to be replaceable
without touching the logic.**

```
core/            <- all business logic. No HTTP, no HTML, no imports of
                    anything outside core/. This is the only part that
                    matters for correctness.
  models.py        typed records everything else passes around
  filters.py       Layer 0 deterministic pre-filter (blacklist/budget sanity)
  security.py      the Sanitizer -- anti-bot trap + prompt-injection detection
  classify.py      Layer 1 structured extraction (domain/archetype/CAD/etc.)
  pricing.py       market estimate, 10%-below-market rule, COGS floor
  strategist.py    explore/exploit strategy ledger per manufacturing domain
  writer.py        proposal drafting (+ seeded fault simulation, see below)
  reviewer.py      deterministic QA gate, independent of the Writer
  pipeline.py      wires Sanitizer->Strategist->Writer->Reviewer per lead
  sentinel.py      drift scoring + hot-swap failover
  orchestrator.py  the Director: runs a full day across channels, owns the
                    agent swarm, the 5-lead/day spawn rule
  llm.py           pluggable LLM boundary (default: free, deterministic)
  db.py            the only module that touches SQLite
  library.py       <-- THE PUBLIC API. Everything outside core/ imports only this.
  channels/        one adapter per lead-source type (see below)

server.py        <- thin REST + static-file server (stdlib http.server).
                    Zero business logic -- every route just calls a
                    core.library function and returns JSON.

ui/              <- pure presentation. Vanilla HTML/CSS/JS, talks to
                    nothing but /api/*. Delete this whole folder and
                    replace it with a React app, a CLI, anything -- core/
                    and server.py don't change.

run_demo.py      <- CLI smoke test, no server needed.
```

`core/library.py` is the seam: it's the only module `server.py` (or a test
suite, or a notebook) is allowed to import from `core/`. If you ever want to
swap SQLite for Postgres, or the stdlib HTTP server for FastAPI, or this
dashboard for something else entirely, this is the file that stays put while
everything on one side or the other changes.

## The five lead channel types

Each one is a `ChannelAdapter` (`core/channels/base.py`) implementing the same
three methods (`list_platforms`, `sub_domains`, `generate_daily_signals`).
The orchestrator doesn't know or care which concrete channel it's driving.

| Channel | File | Platforms modeled | Notes |
|---|---|---|---|
| Freelance & work-for-hire | `channels/freelance.py` | Upwork, Freelancer.com, Cad Crowd, Guru | flat listings, no sub-domain tier |
| B2B trade / RFQ directories | `channels/b2b_trade.py` | Alibaba, Made-in-China, Global Sources, IndiaMART | RFQ sub-categories, MOQ/budget-sanity heavy |
| Community / forums | `channels/community.py` | Reddit, Hackaday, Discord | uneven per-subreddit volume — this is the channel where the 5-lead/day spawn rule actually fires |
| Agency brokerages | `channels/brokerage.py` | Clutch.co, DesignRush, Gembah | low volume, high value, formal RFPs |
| Outbound signals | `channels/outbound.py` | Kickstarter, Indiegogo, USPTO | no stated budget — funding/patent activity implies intent instead |

`generate_daily_signals()` is a **simulator** standing in for the real
scraper (Playwright/Firecrawl/platform API) from the original design. To go
live on a real platform: write a new adapter implementing the same
interface and register it in `core/channels/__init__.py`. Nothing in
`pipeline.py` or `orchestrator.py` needs to change.

## The pipeline, concretely

1. **Layer 0** (`filters.py`) — plain regex/arithmetic, no model call. Drops
   off-category listings (fabric/food/apparel/etc.) and mathematically
   impossible RFQs before anything expensive touches them.
2. **Sanitizer** (`security.py`) — separates legitimate anti-bot constraints
   ("start your proposal with the word X") from prompt-injection attempts
   ("ignore all previous instructions..."). Injections are stripped and
   logged, never passed to the Writer.
3. **Classify** (`classify.py`) — extracts manufacturing domain, client
   archetype, CAD tools, materials, deliverables, project stage.
4. **Pricing** (`pricing.py`) — bids at exactly 10% below an estimated
   market price, **never below a hard COGS + 35%-margin floor** — this floor
   is enforced twice: once when the quote is built, and independently
   re-checked by the Reviewer, so a bug in one stage can't ship an
   underpriced bid on its own.
5. **Strategist** (`strategist.py`) — epsilon-greedy (80% exploit / 20%
   explore) over a per-domain strategy ledger that tracks win rate.
6. **Writer** (`writer.py`) — drafts the proposal from the Strategist's
   blueprint, channel-appropriate tone, and any mandatory anti-bot phrase.
7. **Reviewer** (`reviewer.py`) — deterministic QA gate, re-validates
   everything independently (mandatory phrase present, no leaked injection
   text, price math correct, quoted price actually in the draft). Rejects
   trigger up to 2 rewrites before the bid is marked `REJECTED`.

## Self-healing: Sentinel + hot-swap, actually working

A flat, uniform "5% of drafts have a defect" model turned out to be a bad
demo: with the pricing floor enforced upstream, almost every draft passed
review and the Sentinel never had anything to react to. Real degradation
isn't uniform across a fleet — it's usually **one node** drifting. So agents
are given a per-agent fault bias at spawn time (`core/orchestrator.py`):
~92% stay clean (2% fault rate), ~8% are drawn "degrading" (45% fault rate).
A degrading agent racks up consecutive Reviewer rejections, its EWMA drift
score crosses the hot-swap threshold, and the Sentinel quarantines it and
spins up a clean replacement — visible in the dashboard's Sentinel Events
feed and in the agent table's drift bars. This is the same mechanism
described for production (pin to admin panel, serialize context, hot-swap to
a fallback model), just running against a seeded deterministic fault
generator instead of an actual flaky LLM.

## Running it

No install step — stdlib only.

```bash
# CLI smoke test (prints a full run + one sample pipeline trace)
python3 run_demo.py 5

# Dashboard (REST API + UI)
python3 server.py 8000
# open http://localhost:8000/
```

In the dashboard: **Run Next Cycle** simulates one more day across every
channel (ingest -> filter -> classify -> price -> bid -> review), updating
the funnel, channel table, agent swarm, sentinel feed, strategy ledger, and
leads table (click a row for the full trace + generated proposal text).
**Reset** wipes the database and agent swarm back to day 0.

## Going from simulation to real

Every seam that matters is a single, small file:

- **Real scraping**: implement `ChannelAdapter.generate_daily_signals()` in
  a new adapter (Playwright/Firecrawl/platform API) instead of the mock
  generators in `core/channels/*.py`.
- **Real LLM calls**: `core/llm.py` already has an `AnthropicLLM` client
  behind the same `LLMClient` interface `TemplateLLM` uses — set
  `ANTHROPIC_API_KEY` and pass `use_anthropic=True` to
  `get_default_llm()`. `core.writer.draft_proposal` already accepts an
  `llm` argument.
- **Real pricing data**: `pricing.py`'s `_BASE_MARKET_RATE` table and
  `ALUMINUM_USD_PER_KG` are illustrative placeholders — swap in a live
  comps feed / commodity price API.
- **A different UI**: delete `ui/`, build anything that talks to the
  `/api/*` routes in `server.py` (or replace `server.py` too — the only
  contract that matters is `core/library.py`'s function signatures).
