// Pure presentation layer: fetches from /api/* and renders. No business
// logic lives here -- pricing rules, agent spawn thresholds, sentinel
// drift math etc. all live in core/ and are only ever reflected here as
// numbers/strings the server already computed. That's what makes this
// file replaceable independently of everything else in the project.

const API = {
  channels: () => api("/api/channels"),
  leads: (params) => api("/api/leads?" + new URLSearchParams(params)),
  lead: (id) => api(`/api/leads/${id}`),
  agents: () => api("/api/agents"),
  sentinelEvents: (limit = 30) => api(`/api/sentinel-events?limit=${limit}`),
  strategies: () => api("/api/strategies"),
  funnel: () => api("/api/funnel"),
  status: () => api("/api/status"),
  runCycle: () => api("/api/run-cycle", { method: "POST" }),
  reset: () => api("/api/reset", { method: "POST" }),
};

async function api(path, opts) {
  const res = await fetch(path, opts);
  if (!res.ok) throw new Error(`${path} -> HTTP ${res.status}`);
  return res.json();
}

function fmtMoney(n) {
  if (n === null || n === undefined) return "-";
  return "$" + Number(n).toLocaleString(undefined, { minimumFractionDigits: 2, maximumFractionDigits: 2 });
}

function fmtPct(n) {
  if (n === null || n === undefined) return "-";
  return Number(n).toFixed(1) + "%";
}

function statusPill(status) {
  const map = {
    BID_APPROVED: "pill-ok",
    QUALIFIED: "pill-warn",
    BID_REJECTED_BY_REVIEWER: "pill-bad",
    ACTIVE: "pill-ok",
    IDLE: "pill-idle",
    QUARANTINED: "pill-bad",
    DEPRECATED: "pill-idle",
  };
  const cls = map[status] || "pill-idle";
  return `<span class="pill ${cls}">${status}</span>`;
}

function driftBar(score) {
  const pct = Math.max(0, Math.min(1, score)) * 100;
  const cls = score >= 0.5 ? "bad" : score >= 0.25 ? "warn" : "";
  return `<span class="drift-bar"><span class="drift-bar-fill ${cls}" style="width:${pct}%"></span></span>${score.toFixed(2)}`;
}

// ------------------------------------------------------------------ state

let lastCycleChannels = null; // populated after a run-cycle call

// ------------------------------------------------------------------ render

function renderFunnel(funnel) {
  const t = funnel.totals;
  const cards = [
    ["Raw signals", t.raw_signals],
    ["Layer 0 passed", t.layer0_passed],
    ["Qualified leads", t.qualified],
    ["Bids approved", t.bids_approved],
    ["Bids rejected", t.bids_rejected],
  ];
  document.getElementById("funnel-row").innerHTML = cards
    .map(([label, value]) => `
      <div class="stat-card">
        <div class="stat-label">${label}</div>
        <div class="stat-value">${(value ?? 0).toLocaleString()}</div>
      </div>`)
    .join("");
}

function renderChannelsTable(channelsFromCycle, staticChannels) {
  const rows = (channelsFromCycle && channelsFromCycle.length)
    ? channelsFromCycle.map((c) => ({
        channel_type: c.channel_type,
        platforms: c.platforms,
        signals: c.signals,
        layer0_passed: c.layer0_passed,
        qualified: c.qualified,
        bids_approved: c.bids_approved,
        health: c.channel_health_score,
        spawned: c.subdomain_agents_spawned,
        deprecated: c.subdomain_agents_deprecated,
      }))
    : staticChannels.map((c) => ({
        channel_type: c.channel_type,
        platforms: c.platforms.length,
        signals: "-", layer0_passed: "-", qualified: "-", bids_approved: "-",
        health: "-", spawned: [], deprecated: [],
      }));

  document.querySelector("#table-channels tbody").innerHTML = rows.map((r) => `
    <tr>
      <td>${r.channel_type}</td>
      <td>${r.platforms}</td>
      <td>${r.signals}</td>
      <td>${r.layer0_passed}</td>
      <td>${r.qualified}</td>
      <td>${r.bids_approved}</td>
      <td>${r.health}</td>
      <td class="text-dim">${r.spawned.length ? r.spawned.join(", ") : "-"}</td>
      <td class="text-dim">${r.deprecated.length ? r.deprecated.join(", ") : "-"}</td>
    </tr>`).join("");
}

function renderAgentsTable(agents) {
  const order = { L1_CENTRAL_COMMAND: 0, L2_MACRO_CHANNEL: 1, L3_PLATFORM_WORKER: 2, L4_SUBDOMAIN_WORKER: 3, SENTINEL: 4 };
  agents = [...agents].sort((a, b) => (order[a.layer] ?? 9) - (order[b.layer] ?? 9));
  document.querySelector("#table-agents tbody").innerHTML = agents.map((a) => `
    <tr>
      <td class="text-dim">${a.layer.replace("L1_", "").replace("L2_", "").replace("L3_", "").replace("L4_", "")}</td>
      <td>${a.role}</td>
      <td class="text-dim">${a.channel_type || "-"}${a.sub_domain ? " / " + a.sub_domain : ""}</td>
      <td>${statusPill(a.status)}</td>
      <td class="text-dim">${a.model_signature}</td>
      <td>${driftBar(a.drift_score)}</td>
      <td>${a.leads_handled}</td>
    </tr>`).join("");
}

function renderSentinelFeed(events) {
  const el = document.getElementById("sentinel-feed");
  if (!events.length) {
    el.innerHTML = `<li class="empty">No anomalies detected yet -- run a cycle to generate activity.</li>`;
    return;
  }
  el.innerHTML = events.map((e) => {
    const cls = e.event_type === "HOTSWAP_TRIGGERED" ? "hotswap" : "driftflag";
    const label = e.event_type === "HOTSWAP_TRIGGERED" ? "HOT-SWAP" : "DRIFT FLAG";
    return `<li class="${cls}">
      <span class="event-type">${label}</span>drift=${e.drift_score.toFixed(2)}
      <span class="event-reason">${e.reason}${e.replacement_agent_id ? ` &rarr; replaced by ${e.replacement_agent_id}` : ""}</span>
    </li>`;
  }).join("");
}

function renderStrategiesTable(strategies) {
  document.querySelector("#table-strategies tbody").innerHTML = strategies.map((s) => `
    <tr>
      <td class="text-dim">${s.domain}</td>
      <td>${s.name}</td>
      <td>${s.wins}</td>
      <td>${s.losses}</td>
      <td>${fmtPct(s.win_rate * 100)}</td>
      <td>${s.status}</td>
    </tr>`).join("");
}

function renderLeadsTable(leads) {
  document.querySelector("#table-leads tbody").innerHTML = leads.map((l) => `
    <tr data-lead-id="${l.lead_id}">
      <td>${l.platform}${l.sub_domain ? " / " + l.sub_domain : ""}</td>
      <td>${(l.signal_title || l.title || "").slice(0, 60)}</td>
      <td class="text-dim">${l.client_archetype}</td>
      <td class="text-dim">${l.domain}</td>
      <td>${fmtMoney(l.market_price_usd)}</td>
      <td>${fmtMoney(l.bid_price_usd)}</td>
      <td>${fmtPct(l.margin_pct)}</td>
      <td>${statusPill(l.status)}</td>
    </tr>`).join("");

  document.querySelectorAll("#table-leads tbody tr").forEach((tr) => {
    tr.addEventListener("click", () => openLeadModal(tr.dataset.leadId));
  });
}

async function openLeadModal(leadId) {
  const detail = await API.lead(leadId);
  const lead = detail.lead;
  const bid = detail.bid;
  const trap = JSON.parse(lead.trap_json || "{}");

  const body = `
    <h2 style="margin-top:0">${lead.title}</h2>
    <dl>
      <dt>Platform</dt><dd>${lead.platform}${lead.sub_domain ? " / " + lead.sub_domain : ""}</dd>
      <dt>Channel</dt><dd>${lead.channel_type}</dd>
      <dt>Archetype</dt><dd>${lead.client_archetype}</dd>
      <dt>Domain</dt><dd>${lead.domain}</dd>
      <dt>Project stage</dt><dd>${lead.project_stage}</dd>
      <dt>Market price</dt><dd>${fmtMoney(lead.market_price_usd)}</dd>
      <dt>Bid price</dt><dd>${fmtMoney(lead.bid_price_usd)} (margin ${fmtPct(lead.margin_pct)})</dd>
      <dt>COGS estimate</dt><dd>${fmtMoney(lead.cogs_usd)}</dd>
      <dt>Qualification</dt><dd>${(lead.qualification_score * 100).toFixed(0)}%</dd>
      <dt>Anti-bot trap</dt><dd>${trap.has_anti_bot_phrase ? `yes -- required word: "${trap.required_first_word || ""}"` : "no"}</dd>
      <dt>Prompt injections</dt><dd>${(trap.detected_injections || []).length ? trap.detected_injections.join(" | ") : "none detected"}</dd>
    </dl>
    ${bid ? `
      <h3 style="margin:0 0 8px;font-size:13px;color:var(--text-dim);text-transform:uppercase">
        Proposal &middot; strategy: ${bid.strategy_name}${bid.explore ? " (explore)" : " (exploit)"} &middot; ${statusPill(bid.reviewer_status)}
      </h3>
      <div class="proposal-box">${bid.proposal_text}</div>
      ${JSON.parse(bid.reviewer_notes || "[]").length ? `<div class="text-dim" style="margin-top:8px">Reviewer notes:</div><ul class="notes-list">${JSON.parse(bid.reviewer_notes).map((n) => `<li>${n}</li>`).join("")}</ul>` : ""}
    ` : `<p class="text-dim">No bid was drafted for this lead.</p>`}
  `;
  document.getElementById("modal-body").innerHTML = body;
  document.getElementById("lead-modal").classList.remove("hidden");
}

// ------------------------------------------------------------------ boot

async function refreshAll() {
  const [funnel, agents, events, strategies, status, staticChannels] = await Promise.all([
    API.funnel(), API.agents(), API.sentinelEvents(30), API.strategies(), API.status(), API.channels(),
  ]);

  document.getElementById("day-badge").textContent = `Day ${status.current_day}`;
  renderFunnel(funnel);
  renderChannelsTable(lastCycleChannels, staticChannels);
  renderAgentsTable(agents);
  renderSentinelFeed(events);
  renderStrategiesTable(strategies);

  const channelFilter = document.getElementById("filter-channel");
  if (!channelFilter.dataset.populated) {
    channelFilter.innerHTML += staticChannels.map((c) => `<option value="${c.channel_type}">${c.channel_type}</option>`).join("");
    channelFilter.dataset.populated = "1";
  }

  await refreshLeads();
}

async function refreshLeads() {
  const params = { limit: "150" };
  const status = document.getElementById("filter-status").value;
  const channel = document.getElementById("filter-channel").value;
  if (status) params.status = status;
  if (channel) params.channel_type = channel;
  const leads = await API.leads(params);
  renderLeadsTable(leads);
}

function setBusy(busy) {
  document.getElementById("btn-run").disabled = busy;
  document.getElementById("btn-reset").disabled = busy;
}

document.getElementById("btn-run").addEventListener("click", async () => {
  setBusy(true);
  try {
    const summary = await API.runCycle();
    lastCycleChannels = summary.channels;
    await refreshAll();
  } catch (err) {
    alert("Run cycle failed: " + err.message);
  } finally {
    setBusy(false);
  }
});

document.getElementById("btn-reset").addEventListener("click", async () => {
  if (!confirm("Reset the database and agent swarm back to day 0?")) return;
  setBusy(true);
  try {
    await API.reset();
    lastCycleChannels = null;
    await refreshAll();
  } finally {
    setBusy(false);
  }
});

document.getElementById("filter-status").addEventListener("change", refreshLeads);
document.getElementById("filter-channel").addEventListener("change", refreshLeads);
document.getElementById("modal-close").addEventListener("click", () => document.getElementById("lead-modal").classList.add("hidden"));
document.getElementById("lead-modal").addEventListener("click", (e) => {
  if (e.target.id === "lead-modal") e.target.classList.add("hidden");
});

refreshAll().catch((err) => console.error("initial load failed", err));
