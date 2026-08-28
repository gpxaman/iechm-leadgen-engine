#include "store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------- helpers */

static void list_to_json(const char items[][LIST_ITEM_LEN], int count, char *out, size_t outcap) {
    Jsonw w;
    jw_init(&w);
    jw_arr_open(&w);
    for (int i = 0; i < count; i++) jw_str(&w, items[i]);
    jw_arr_close(&w);
    char *s = jw_finish(&w);
    xcpy(out, outcap, s);
    free(s);
}

static void notes_to_json(const char items[][NOTE_LEN], int count, char *out, size_t outcap) {
    Jsonw w;
    jw_init(&w);
    jw_arr_open(&w);
    for (int i = 0; i < count; i++) jw_str(&w, items[i]);
    jw_arr_close(&w);
    char *s = jw_finish(&w);
    xcpy(out, outcap, s);
    free(s);
}

static const StoredSignal *find_signal_by_id(const Store *st, const char *signal_id) {
    for (int i = 0; i < st->signals.count; i++) {
        const StoredSignal *s = vec_at(&st->signals, i);
        if (strcmp(s->signal_id, signal_id) == 0) return s;
    }
    return NULL;
}

static const StoredLead *find_lead_by_id(const Store *st, const char *lead_id) {
    for (int i = 0; i < st->leads.count; i++) {
        const StoredLead *l = vec_at(&st->leads, i);
        if (strcmp(l->lead_id, lead_id) == 0) return l;
    }
    return NULL;
}

/* Most recently created bid for a lead -- bids are appended in creation
 * order, so the first hit scanning backwards is the most recent one. */
static const StoredBid *find_latest_bid_for_lead(const Store *st, const char *lead_id) {
    for (int i = st->bids.count - 1; i >= 0; i--) {
        const StoredBid *b = vec_at(&st->bids, i);
        if (strcmp(b->lead_id, lead_id) == 0) return b;
    }
    return NULL;
}

/* -------------------------------------------------------------------- init */

void store_init(Store *st) {
    vec_init(&st->signals, sizeof(StoredSignal));
    vec_init(&st->leads, sizeof(StoredLead));
    vec_init(&st->bids, sizeof(StoredBid));
    vec_init(&st->agents, sizeof(StoredAgent));
    vec_init(&st->sentinel_events, sizeof(StoredSentinelEvent));
    vec_init(&st->strategies, sizeof(StoredStrategy));
    vec_init(&st->run_log, sizeof(RunLogEntry));
    st->next_seq = 1;
}

void store_reset(Store *st) {
    vec_clear(&st->signals);
    vec_clear(&st->leads);
    vec_clear(&st->bids);
    vec_clear(&st->agents);
    vec_clear(&st->sentinel_events);
    vec_clear(&st->strategies);
    vec_clear(&st->run_log);
    st->next_seq = 1;
}

/* ------------------------------------------------------------------- save */

void store_save_signal(Store *st, const RawSignal *signal, int day, bool layer0_passed, const char *layer0_reason) {
    /* signal_id is always freshly generated (see new_id), so this is
     * effectively always an insert -- "INSERT OR REPLACE" in the original
     * only matters on a primary-key collision, which random 64-bit ids
     * never produce in practice. */
    StoredSignal *row = vec_push(&st->signals);
    row->seq = st->next_seq++;
    xcpy(row->signal_id, ID_LEN, signal->signal_id);
    xcpy(row->channel_type, SHORT_LEN, channel_type_str(signal->channel_type));
    xcpy(row->platform, SHORT_LEN, signal->platform);
    xcpy(row->sub_domain, SHORT_LEN, signal->sub_domain);
    xcpy(row->title, TITLE_LEN, signal->title);
    xcpy(row->raw_text, TEXT_LEN, signal->raw_text);
    row->has_budget = signal->has_budget;
    row->stated_budget_usd = signal->stated_budget_usd;
    row->has_volume = signal->has_volume;
    row->target_volume = signal->target_volume;
    xcpy(row->url, URL_LEN, signal->url);
    xcpy(row->posted_at, SHORT_LEN, signal->posted_at);
    row->layer0_passed = layer0_passed ? 1 : 0;
    xcpy(row->layer0_reason, SHORT_LEN, layer0_reason);
    row->day = day;
}

void store_save_lead(Store *st, const QualifiedLead *lead) {
    StoredLead *row = vec_push(&st->leads);
    row->seq = st->next_seq++;
    xcpy(row->lead_id, ID_LEN, lead->lead_id);
    xcpy(row->signal_id, ID_LEN, lead->signal.signal_id);
    xcpy(row->client_archetype, SHORT_LEN, archetype_str(lead->client_archetype));
    xcpy(row->domain, SHORT_LEN, domain_str(lead->domain));
    list_to_json(lead->cad_software, lead->cad_software_count, row->cad_software_json, JSONTEXT_LEN);
    list_to_json(lead->materials, lead->materials_count, row->materials_json, JSONTEXT_LEN);
    list_to_json(lead->deliverables, lead->deliverables_count, row->deliverables_json, JSONTEXT_LEN);
    xcpy(row->project_stage, SHORT_LEN, lead->project_stage);
    row->has_target_volume = lead->has_target_volume;
    row->target_volume = lead->target_volume;
    row->market_price_usd = lead->market_price_usd;
    row->bid_price_usd = lead->bid_price_usd;
    row->cogs_usd = lead->cogs_usd;
    row->margin_pct = lead->margin_pct;
    row->qualification_score = lead->qualification_score;
    trap_analysis_to_json(&lead->trap_analysis, row->trap_json, TEXT_LEN);
    xcpy(row->status, SHORT_LEN, lead_status_str(lead->status));
    xcpy(row->created_at, SHORT_LEN, lead->created_at);
}

void store_save_bid(Store *st, const BidRecord *bid) {
    StoredBid *row = vec_push(&st->bids);
    row->seq = st->next_seq++;
    xcpy(row->bid_id, ID_LEN, bid->bid_id);
    xcpy(row->lead_id, ID_LEN, bid->lead_id);
    xcpy(row->strategy_id, STRATEGY_ID_LEN, bid->strategy_id);
    xcpy(row->strategy_name, SHORT_LEN, bid->strategy_name);
    row->explore = bid->explore ? 1 : 0;
    xcpy(row->proposal_text, TEXT_LEN, bid->proposal_text);
    row->price_usd = bid->price_usd;
    xcpy(row->reviewer_status, SHORT_LEN, bid->reviewer_status);
    notes_to_json(bid->reviewer_notes, bid->reviewer_notes_count, row->reviewer_notes_json, JSONTEXT_LEN);
    row->rewrite_count = bid->rewrite_count;
    xcpy(row->created_at, SHORT_LEN, bid->created_at);
}

static void fill_agent_row(StoredAgent *row, const AgentNode *agent) {
    xcpy(row->agent_id, ID_LEN, agent->agent_id);
    xcpy(row->layer, SHORT_LEN, agent_layer_str(agent->layer));
    xcpy(row->role, ROLE_LEN, agent->role);
    xcpy(row->channel_type, SHORT_LEN, agent->has_channel_type ? channel_type_str(agent->channel_type) : "");
    xcpy(row->sub_domain, SHORT_LEN, agent->has_sub_domain ? agent->sub_domain : "");
    xcpy(row->status, SHORT_LEN, agent_status_str(agent->status));
    xcpy(row->model_signature, SHORT_LEN, agent->model_signature);
    row->drift_score = agent->drift_score;
    row->consecutive_flags = agent->consecutive_flags;
    row->leads_handled = agent->leads_handled;
    xcpy(row->spawned_at, SHORT_LEN, agent->spawned_at);
}

void store_save_agent(Store *st, const AgentNode *agent) {
    for (int i = 0; i < st->agents.count; i++) {
        StoredAgent *row = vec_at(&st->agents, i);
        if (strcmp(row->agent_id, agent->agent_id) == 0) { fill_agent_row(row, agent); return; }
    }
    StoredAgent *row = vec_push(&st->agents);
    row->seq = st->next_seq++;
    fill_agent_row(row, agent);
}

void store_save_sentinel_event(Store *st, const SentinelEvent *event) {
    StoredSentinelEvent *row = vec_push(&st->sentinel_events);
    row->seq = st->next_seq++;
    xcpy(row->event_id, ID_LEN, event->event_id);
    xcpy(row->agent_id, ID_LEN, event->agent_id);
    xcpy(row->event_type, SHORT_LEN, event->event_type);
    row->drift_score = event->drift_score;
    xcpy(row->reason, TEXT_LEN, event->reason);
    xcpy(row->replacement_agent_id, ID_LEN, event->has_replacement ? event->replacement_agent_id : "");
    xcpy(row->created_at, SHORT_LEN, event->created_at);
}

static void fill_strategy_row(StoredStrategy *row, const StrategyRecord *rec) {
    xcpy(row->strategy_id, STRATEGY_ID_LEN, rec->strategy_id);
    xcpy(row->domain, SHORT_LEN, domain_str(rec->domain));
    xcpy(row->name, SHORT_LEN, rec->name);
    row->wins = rec->wins;
    row->losses = rec->losses;
    xcpy(row->status, SHORT_LEN, rec->status);
}

void store_save_strategy(Store *st, const StrategyRecord *rec) {
    for (int i = 0; i < st->strategies.count; i++) {
        StoredStrategy *row = vec_at(&st->strategies, i);
        if (strcmp(row->strategy_id, rec->strategy_id) == 0) { fill_strategy_row(row, rec); return; }
    }
    StoredStrategy *row = vec_push(&st->strategies);
    row->seq = st->next_seq++;
    fill_strategy_row(row, rec);
}

void store_save_run_log(Store *st, int day, const char *channel_type, int signals, int layer0_passed,
                         int qualified, int approved, int rejected, const char *created_at) {
    RunLogEntry *row = vec_push(&st->run_log);
    row->seq = st->next_seq++;
    row->day = day;
    xcpy(row->channel_type, SHORT_LEN, channel_type);
    row->signals_count = signals;
    row->layer0_passed = layer0_passed;
    row->qualified = qualified;
    row->bids_approved = approved;
    row->bids_rejected = rejected;
    xcpy(row->created_at, SHORT_LEN, created_at);
}

/* ------------------------------------------------------------ JSON reads */

/* `seq` is assigned in strictly increasing insertion order and created_at
 * is a coarse rounding of the same monotonic clock, so walking a vec from
 * its last index backwards already yields "ORDER BY created_at DESC" (with
 * ties broken by insertion order) -- no separate sort needed. */

void store_write_leads_json(const Store *st, Jsonw *w, int limit, const char *status_filter, const char *channel_type_filter) {
    bool want_status = status_filter && *status_filter;
    bool want_channel = channel_type_filter && *channel_type_filter;

    jw_arr_open(w);
    int emitted = 0;
    for (int i = st->leads.count - 1; i >= 0 && emitted < limit; i--) {
        const StoredLead *lead = vec_at(&st->leads, i);
        if (want_status && strcmp(lead->status, status_filter) != 0) continue;

        const StoredSignal *sig = find_signal_by_id(st, lead->signal_id);
        const char *platform = sig ? sig->platform : "";
        const char *channel_type = sig ? sig->channel_type : "";
        const char *sub_domain = sig ? sig->sub_domain : "";
        const char *signal_title = sig ? sig->title : "";

        if (want_channel && strcmp(channel_type, channel_type_filter) != 0) continue;

        jw_obj_open(w);
        jw_kv_str(w, "lead_id", lead->lead_id);
        jw_kv_str(w, "signal_id", lead->signal_id);
        jw_kv_str(w, "client_archetype", lead->client_archetype);
        jw_kv_str(w, "domain", lead->domain);
        jw_kv_str(w, "cad_software", lead->cad_software_json);
        jw_kv_str(w, "materials", lead->materials_json);
        jw_kv_str(w, "deliverables", lead->deliverables_json);
        jw_kv_str(w, "project_stage", lead->project_stage);
        jw_kv_int_or_null(w, "target_volume", lead->target_volume, lead->has_target_volume);
        jw_kv_dbl(w, "market_price_usd", lead->market_price_usd, 2);
        jw_kv_dbl(w, "bid_price_usd", lead->bid_price_usd, 2);
        jw_kv_dbl(w, "cogs_usd", lead->cogs_usd, 2);
        jw_kv_dbl(w, "margin_pct", lead->margin_pct, 2);
        jw_kv_dbl(w, "qualification_score", lead->qualification_score, 3);
        jw_kv_str(w, "trap_json", lead->trap_json);
        jw_kv_str(w, "status", lead->status);
        jw_kv_str(w, "created_at", lead->created_at);
        jw_kv_str(w, "platform", platform);
        jw_kv_str(w, "channel_type", channel_type);
        jw_kv_str(w, "sub_domain", sub_domain);
        jw_kv_str(w, "signal_title", signal_title);
        jw_obj_close(w);
        emitted++;
    }
    jw_arr_close(w);
}

bool store_write_lead_detail_json(const Store *st, Jsonw *w, const char *lead_id) {
    const StoredLead *lead = find_lead_by_id(st, lead_id);
    if (!lead) return false;
    const StoredSignal *sig = find_signal_by_id(st, lead->signal_id);
    const StoredBid *bid = find_latest_bid_for_lead(st, lead_id);

    jw_obj_open(w);
    jw_key(w, "lead");
    jw_obj_open(w);
    jw_kv_str(w, "lead_id", lead->lead_id);
    jw_kv_str(w, "signal_id", lead->signal_id);
    jw_kv_str(w, "client_archetype", lead->client_archetype);
    jw_kv_str(w, "domain", lead->domain);
    jw_kv_str(w, "cad_software", lead->cad_software_json);
    jw_kv_str(w, "materials", lead->materials_json);
    jw_kv_str(w, "deliverables", lead->deliverables_json);
    jw_kv_str(w, "project_stage", lead->project_stage);
    jw_kv_int_or_null(w, "target_volume", lead->target_volume, lead->has_target_volume);
    jw_kv_dbl(w, "market_price_usd", lead->market_price_usd, 2);
    jw_kv_dbl(w, "bid_price_usd", lead->bid_price_usd, 2);
    jw_kv_dbl(w, "cogs_usd", lead->cogs_usd, 2);
    jw_kv_dbl(w, "margin_pct", lead->margin_pct, 2);
    jw_kv_dbl(w, "qualification_score", lead->qualification_score, 3);
    jw_kv_str(w, "trap_json", lead->trap_json);
    jw_kv_str(w, "status", lead->status);
    jw_kv_str(w, "created_at", lead->created_at);
    jw_kv_str(w, "channel_type", sig ? sig->channel_type : "");
    jw_kv_str(w, "platform", sig ? sig->platform : "");
    jw_kv_str(w, "sub_domain", sig ? sig->sub_domain : "");
    jw_kv_str(w, "title", sig ? sig->title : "");
    jw_kv_str(w, "raw_text", sig ? sig->raw_text : "");
    jw_kv_dbl_or_null(w, "stated_budget_usd", sig ? sig->stated_budget_usd : 0.0, 2, sig && sig->has_budget);
    jw_kv_str(w, "url", sig ? sig->url : "");
    jw_kv_str(w, "posted_at", sig ? sig->posted_at : "");
    jw_kv_int(w, "layer0_passed", sig ? sig->layer0_passed : 0);
    jw_kv_str(w, "layer0_reason", sig ? sig->layer0_reason : "");
    jw_kv_int(w, "day", sig ? sig->day : 0);
    jw_obj_close(w);

    jw_key(w, "bid");
    if (bid) {
        jw_obj_open(w);
        jw_kv_str(w, "bid_id", bid->bid_id);
        jw_kv_str(w, "lead_id", bid->lead_id);
        jw_kv_str(w, "strategy_id", bid->strategy_id);
        jw_kv_str(w, "strategy_name", bid->strategy_name);
        jw_kv_int(w, "explore", bid->explore);
        jw_kv_str(w, "proposal_text", bid->proposal_text);
        jw_kv_dbl(w, "price_usd", bid->price_usd, 2);
        jw_kv_str(w, "reviewer_status", bid->reviewer_status);
        jw_kv_str(w, "reviewer_notes", bid->reviewer_notes_json);
        jw_kv_int(w, "rewrite_count", bid->rewrite_count);
        jw_kv_str(w, "created_at", bid->created_at);
        jw_obj_close(w);
    } else {
        jw_null(w);
    }
    jw_obj_close(w);
    return true;
}

void store_write_agents_json(const Store *st, Jsonw *w) {
    int n = st->agents.count;
    int *idx = n ? malloc(sizeof(int) * (size_t) n) : NULL;
    for (int i = 0; i < n; i++) idx[i] = i;
    /* stable insertion sort by (layer, channel_type) ascending, matching
     * `ORDER BY layer, channel_type` -- n is always small (tens of agents). */
    for (int i = 1; i < n; i++) {
        int key = idx[i];
        const StoredAgent *ka = vec_at(&st->agents, key);
        int j = i - 1;
        while (j >= 0) {
            const StoredAgent *ja = vec_at(&st->agents, idx[j]);
            int c = strcmp(ja->layer, ka->layer);
            if (c == 0) c = strcmp(ja->channel_type, ka->channel_type);
            if (c <= 0) break;
            idx[j + 1] = idx[j];
            j--;
        }
        idx[j + 1] = key;
    }

    jw_arr_open(w);
    for (int i = 0; i < n; i++) {
        const StoredAgent *a = vec_at(&st->agents, idx[i]);
        jw_obj_open(w);
        jw_kv_str(w, "agent_id", a->agent_id);
        jw_kv_str(w, "layer", a->layer);
        jw_kv_str(w, "role", a->role);
        jw_kv_str(w, "channel_type", a->channel_type);
        jw_kv_str(w, "sub_domain", a->sub_domain);
        jw_kv_str(w, "status", a->status);
        jw_kv_str(w, "model_signature", a->model_signature);
        jw_kv_dbl(w, "drift_score", a->drift_score, 4);
        jw_kv_int(w, "consecutive_flags", a->consecutive_flags);
        jw_kv_int(w, "leads_handled", a->leads_handled);
        jw_kv_str(w, "spawned_at", a->spawned_at);
        jw_obj_close(w);
    }
    jw_arr_close(w);
    free(idx);
}

void store_write_sentinel_events_json(const Store *st, Jsonw *w, int limit) {
    jw_arr_open(w);
    int emitted = 0;
    for (int i = st->sentinel_events.count - 1; i >= 0 && emitted < limit; i--) {
        const StoredSentinelEvent *e = vec_at(&st->sentinel_events, i);
        jw_obj_open(w);
        jw_kv_str(w, "event_id", e->event_id);
        jw_kv_str(w, "agent_id", e->agent_id);
        jw_kv_str(w, "event_type", e->event_type);
        jw_kv_dbl(w, "drift_score", e->drift_score, 4);
        jw_kv_str(w, "reason", e->reason);
        jw_kv_str(w, "replacement_agent_id", e->replacement_agent_id);
        jw_kv_str(w, "created_at", e->created_at);
        jw_obj_close(w);
        emitted++;
    }
    jw_arr_close(w);
}

void store_write_strategies_json(const Store *st, Jsonw *w) {
    int n = st->strategies.count;
    int *idx = n ? malloc(sizeof(int) * (size_t) n) : NULL;
    for (int i = 0; i < n; i++) idx[i] = i;
    for (int i = 1; i < n; i++) {
        int key = idx[i];
        const StoredStrategy *ka = vec_at(&st->strategies, key);
        int j = i - 1;
        while (j >= 0) {
            const StoredStrategy *ja = vec_at(&st->strategies, idx[j]);
            int c = strcmp(ja->domain, ka->domain);
            if (c == 0) c = strcmp(ja->name, ka->name);
            if (c <= 0) break;
            idx[j + 1] = idx[j];
            j--;
        }
        idx[j + 1] = key;
    }

    jw_arr_open(w);
    for (int i = 0; i < n; i++) {
        const StoredStrategy *s = vec_at(&st->strategies, idx[i]);
        int total = s->wins + s->losses;
        double win_rate = total ? (double) s->wins / (double) total : 0.0;
        jw_obj_open(w);
        jw_kv_str(w, "strategy_id", s->strategy_id);
        jw_kv_str(w, "domain", s->domain);
        jw_kv_str(w, "name", s->name);
        jw_kv_int(w, "wins", s->wins);
        jw_kv_int(w, "losses", s->losses);
        jw_kv_dbl(w, "win_rate", win_rate, 3);
        jw_kv_str(w, "status", s->status);
        jw_obj_close(w);
    }
    jw_arr_close(w);
    free(idx);
}

void store_write_funnel_json(const Store *st, Jsonw *w) {
    long long tot_signals = 0, tot_l0 = 0, tot_q = 0, tot_a = 0, tot_r = 0;
    for (int i = 0; i < st->run_log.count; i++) {
        const RunLogEntry *e = vec_at(&st->run_log, i);
        tot_signals += e->signals_count;
        tot_l0 += e->layer0_passed;
        tot_q += e->qualified;
        tot_a += e->bids_approved;
        tot_r += e->bids_rejected;
    }

    jw_obj_open(w);
    jw_key(w, "totals");
    jw_obj_open(w);
    jw_kv_int(w, "raw_signals", tot_signals);
    jw_kv_int(w, "layer0_passed", tot_l0);
    jw_kv_int(w, "qualified", tot_q);
    jw_kv_int(w, "bids_approved", tot_a);
    jw_kv_int(w, "bids_rejected", tot_r);
    jw_obj_close(w);

    jw_key(w, "by_channel");
    jw_arr_open(w);
    static const ChannelType order[] = {CT_FREELANCE, CT_B2B_TRADE, CT_COMMUNITY, CT_BROKERAGE, CT_OUTBOUND};
    for (int c = 0; c < 5; c++) {
        const char *ct = channel_type_str(order[c]);
        long long s = 0, l0 = 0, q = 0, a = 0;
        bool any = false;
        for (int i = 0; i < st->run_log.count; i++) {
            const RunLogEntry *e = vec_at(&st->run_log, i);
            if (strcmp(e->channel_type, ct) != 0) continue;
            any = true;
            s += e->signals_count;
            l0 += e->layer0_passed;
            q += e->qualified;
            a += e->bids_approved;
        }
        if (!any) continue;
        jw_obj_open(w);
        jw_kv_str(w, "channel_type", ct);
        jw_kv_int(w, "s", s);
        jw_kv_int(w, "l0", l0);
        jw_kv_int(w, "q", q);
        jw_kv_int(w, "a", a);
        jw_obj_close(w);
    }
    jw_arr_close(w);
    jw_obj_close(w);
}

/* ------------------------------------------------------- disk persistence */

#define STORE_MAGIC "IECHMDB1"

static void write_vec(FILE *f, const Vec *v) {
    uint64_t count = (uint64_t) v->count;
    fwrite(&count, sizeof count, 1, f);
    if (count) fwrite(v->items, v->elem_size, (size_t) count, f);
}

static bool read_vec(FILE *f, Vec *v) {
    uint64_t count;
    if (fread(&count, sizeof count, 1, f) != 1) return false;
    vec_reserve(v, (int) count);
    if (count && fread(v->items, v->elem_size, (size_t) count, f) != count) return false;
    v->count = (int) count;
    return true;
}

void store_save_to_disk(const Store *st, const char *path) {
    char tmp_path[512];
    snprintf(tmp_path, sizeof tmp_path, "%s.tmp", path);
    FILE *f = fopen(tmp_path, "wb");
    if (!f) { fprintf(stderr, "store: could not open %s for writing\n", tmp_path); return; }

    fwrite(STORE_MAGIC, 1, 8, f);
    write_vec(f, &st->signals);
    write_vec(f, &st->leads);
    write_vec(f, &st->bids);
    write_vec(f, &st->agents);
    write_vec(f, &st->sentinel_events);
    write_vec(f, &st->strategies);
    write_vec(f, &st->run_log);
    fwrite(&st->next_seq, sizeof st->next_seq, 1, f);
    fclose(f);

    rename(tmp_path, path); /* atomic replace, avoids a torn file on crash */
}

bool store_load_from_disk(Store *st, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    char magic[8];
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, STORE_MAGIC, 8) != 0) {
        fprintf(stderr, "store: %s is not a recognized snapshot, ignoring\n", path);
        fclose(f);
        return false;
    }

    bool ok = read_vec(f, &st->signals) && read_vec(f, &st->leads) && read_vec(f, &st->bids) &&
              read_vec(f, &st->agents) && read_vec(f, &st->sentinel_events) &&
              read_vec(f, &st->strategies) && read_vec(f, &st->run_log) &&
              fread(&st->next_seq, sizeof st->next_seq, 1, f) == 1;
    fclose(f);
    if (!ok) {
        fprintf(stderr, "store: %s was truncated/corrupt, starting empty\n", path);
        store_reset(st);
        return false;
    }
    return true;
}
