# IECHM Lead Generation Engine (C port)

A from-scratch C rewrite of the Python backend in `../iechm-leadgen-engine/`.
Same architecture, same pipeline, same REST API, same UI (copied over
unchanged from `ui/` since it only ever talks to `/api/*`). This directory
is fully independent of the Python project -- nothing there was modified.

## Build & run

Requires only a C compiler and `make` -- no external libraries beyond libc,
libm, and libpthread (all standard on any Linux box).

```bash
sudo apt-get install build-essential   # if you don't already have gcc/make
cd iechm-leadgen-engine-c
make

./iechm_server 8000        # dashboard + REST API, open http://localhost:8000/
./iechm_demo 5              # CLI smoke test, 5 simulated days, no server needed
```

Both binaries resolve `ui/` and `iechm.db` relative to their own location
(via `/proc/self/exe`), so they work regardless of your current directory.
`iechm.db` here is a **different file format** from the Python version's
sqlite database -- see "Persistence" below -- so the two projects' data
directories don't collide, but also don't share data.

## Why the code is shaped the way it is

This was written in an environment with **no C compiler available** to
build or test against, so every design choice below leans toward "safe and
inspectable by reading" over "clever" or "matches the Python 1:1 at the
implementation level." The Python's structure and every business rule are
preserved exactly; what changed is *how* a few cross-cutting things
(regex, persistence, JSON, HTTP) are implemented, because their Python
implementations aren't things C has for free.

- **No `<regex.h>`.** Whether glibc's regex engine supports the GNU escapes
  (`\d`, `\w`, `\b`) the original patterns use, and how, isn't something
  that could be verified here. Every pattern in `filters.py`/`security.py`/
  `classify.py` was hand-translated instead into explicit word-boundary/
  substring matchers (`strutil.c`) or small dedicated scanners (the two
  patterns with real quantifiers: a digit run, and `\s*`) -- each a direct,
  checkable translation of one regex line, not a general regex
  reimplementation.
- **Fixed-capacity buffers, not dynamic strings, for record fields.**
  `QualifiedLead`, `BidRecord`, etc. use `char field[N]` throughout rather
  than `malloc`'d strings. This trades a bounded amount of memory for
  eliminating use-after-free/double-free risk across the whole model layer
  -- exactly the class of bug that's hardest to catch without a compiler
  and sanitizers. The *store* tables (unbounded row counts) are the
  exception: `vec.c` is a small, single-purpose growable array, the one
  place realloc is used.
- **No SQLite.** No sqlite3 development headers were available either, and
  the goal was zero external dependencies (matching the Python's own
  "stdlib only" ethos) rather than vendoring an amalgamation. `store.c`
  keeps every table as an in-memory array and persists the whole `Store` as
  a raw struct-layout snapshot to `iechm.db` after every mutating call
  (`store.h` has the full rationale). It's this process's own memory
  layout, not a portable file format -- a fair trade against sqlite's own
  file-format specificity.
- **Hand-rolled JSON writer, no parser** beyond one ad hoc scan for the
  single POST-body shape this API ever receives (`{"day": N}`). A general
  parser would be pure overhead for that.
- **Raw sockets + one detached pthread per connection**, mirroring
  Python's `ThreadingHTTPServer`. Unlike the Python version -- where every
  read opens its own sqlite connection and only concurrent *writes* to the
  in-memory Orchestrator were ever at risk -- every request here touches
  the same in-memory `Store`/`Orchestrator`, including `Vec`s that
  `realloc`. `library.c` wraps every call in one mutex so a GET can never
  observe a `Vec` mid-`realloc` from a concurrent POST.

## Deliberate small deviations from the Python

- **`/api/strategies` now includes `win_rate`.** The Python's
  `StrategyRecord.to_dict()` computes it, but the actual read path
  (`db.get_strategies`) is a raw `SELECT *` that never joins it in, so the
  live API silently omits it and the dashboard's win-rate column reads
  `NaN%`. That looked like a bug rather than intentional behavior, so this
  port computes it at read time.
- **Optional/nullable string fields render as `""` rather than `null`**
  (agent `channel_type`/`sub_domain`, sentinel event `replacement_agent_id`).
  The dashboard only ever does truthy checks on these (`a.channel_type ||
  "-"`), so `""` and `null` render identically -- and this sidesteps a
  three-state (set / explicitly-null / absent) representation for every
  optional string in the model. Nullable *numbers* (`stated_budget_usd`,
  `target_volume`) do still serialize as real JSON `null`, matching the
  original, since the dashboard doesn't touch them either way and a
  numeric field silently becoming `0` would be a real behavior change.
- Every other field name, JSON shape, pricing rule, threshold, and
  probability is carried over exactly (10% below market, 35% margin floor,
  0.35/0.5/3 sentinel EWMA constants, 5/2 subdomain spawn thresholds, 0.08
  degrading-agent probability, etc.) -- see the corresponding `.c` file's
  header comment for the Python file it mirrors.

## Layout

Flat `src/` (no nested `include/`), one `.h`/`.c` pair per `core/*.py`
module plus a few C-only supporting modules:

| Python | C | |
|---|---|---|
| `core/models.py` | `models.h/.c` | |
| `core/filters.py` | `filters.h/.c` | |
| `core/security.py` | `security.h/.c` | |
| `core/classify.py` | `classify.h/.c` | |
| `core/pricing.py` | `pricing.h/.c` | |
| `core/strategist.py` | `strategist.h/.c` | |
| `core/writer.py` | `writer.h/.c` | |
| `core/reviewer.py` | `reviewer.h/.c` | |
| `core/pipeline.py` | `pipeline.h/.c` | |
| `core/sentinel.py` | `sentinel.h/.c` | |
| `core/orchestrator.py` | `orchestrator.h/.c` | |
| `core/db.py` | `store.h/.c` | in-memory + flat-file, not sqlite |
| `core/library.py` | `library.h/.c` | returns JSON strings, not dicts |
| `core/channels/*.py` | `channel_*.c`, `channels_util.*`, `channels_registry.c` | |
| `server.py` | `http.c`, `main.c` | raw sockets, not `http.server` |
| `run_demo.py` | `run_demo.c` | reads `store.c`/`orchestrator.c` directly, not through `library.c`'s JSON |
| -- | `rng.c` | seeded PRNG standing in for `random.Random(seed)` |
| -- | `strutil.c` | word-boundary/substring matching standing in for `re` |
| -- | `jsonw.c` | JSON encoder |
| -- | `vec.c` | the one growable array, used only by `store.c` |
| -- | `fmt.c` | `f"{x:,.2f}"`-style number formatting |

`core.llm`'s pluggable LLM boundary was not ported: the whole pipeline runs
in the deterministic `TemplateLLM` mode in the Python too (nothing calls a
real model), and wiring a real Anthropic client through a C binary raises
questions -- which SDK, TLS, JSON-over-HTTP client -- well outside "port
the deterministic simulation faithfully." The seam (`writer.c` taking an
`llm` argument) can be added the same way the Python's was, if this ever
needs to go live.

## Verifying this build

None of this has been compiled -- the environment it was written in had no
C toolchain. After `make` succeeds:

```bash
./iechm_demo 5        # exercises the whole pipeline end-to-end, no server
./iechm_server 8000    # then open http://localhost:8000/, click "Run Next Cycle"
```

If `make` reports errors, they're most likely to be in the areas noted
above as untested-by-construction (signature drift between a `.h` and its
`.c`, a missed include) rather than in the business logic itself, which was
translated line-by-line against the Python source.
