/* CLI smoke test -- mirrors run_demo.py. Talks to store.c/orchestrator.c
 * directly rather than through library.c's JSON strings: library.c exists
 * to serve http.c, and a CLI printing typed fields has no reason to
 * serialize to JSON and back. Persists to the same iechm.db next to the
 * binary that the server uses, so a demo run and the dashboard can look at
 * the same data, exactly like the original (both go through core.library).
 *
 * Usage: ./iechm_demo [num_days]
 */
#include "store.h"
#include "orchestrator.h"
#include "models.h"
#include "fmt.h"
#include "rng.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void get_exe_dir(char *out, size_t cap) {
    char path[4096];
    ssize_t n = readlink("/proc/self/exe", path, sizeof path - 1);
    if (n <= 0) { xcpy(out, cap, "."); return; }
    path[n] = '\0';
    char *slash = strrchr(path, '/');
    if (slash) *slash = '\0';
    xcpy(out, cap, path);
}

int main(int argc, char **argv) {
    global_rng_seed();
    int days = argc > 1 ? atoi(argv[1]) : 3;

    char exe_dir[2048], db_path[2048];
    get_exe_dir(exe_dir, sizeof exe_dir);
    snprintf(db_path, sizeof db_path, "%s/iechm.db", exe_dir);

    Store store;
    store_init(&store);
    Orchestrator orch;
    orchestrator_init(&orch);
    store_save_to_disk(&store, db_path); /* reset_all()'s effect: start from empty */

    printf("IECHM Lead Generation Engine -- running %d simulated day(s)\n\n", days);

    for (int d = 1; d <= days; d++) {
        CycleSummary summary;
        orchestrator_run_cycle(&orch, &store, d, &summary);
        store_save_to_disk(&store, db_path);

        printf("=== Day %d ===\n", summary.day);
        printf("  raw signals ingested : %d\n", summary.total_signals);
        printf("  qualified leads      : %d\n", summary.total_qualified);
        printf("  bids approved        : %d\n", summary.total_bids_approved);
        printf("  sentinel events      : %d\n", summary.sentinel_events_this_cycle);
        for (int i = 0; i < summary.channels_count; i++) {
            const ChannelCycleSummary *ch = &summary.channels[i];
            printf("    - %-24s signals=%-4d qualified=%-4d bids_ok=%-3d health=%g\n",
                   ch->channel_type, ch->signals, ch->qualified, ch->bids_approved, ch->channel_health_score);
            if (ch->spawned_count) {
                printf("        spawned L4 workers: [");
                for (int j = 0; j < ch->spawned_count; j++) printf("%s'%s'", j ? ", " : "", ch->spawned[j]);
                printf("]\n");
            }
            if (ch->deprecated_count) {
                printf("        deprecated L4 workers: [");
                for (int j = 0; j < ch->deprecated_count; j++) printf("%s'%s'", j ? ", " : "", ch->deprecated[j]);
                printf("]\n");
            }
        }
        printf("\n");
    }

    printf("=== Funnel (cumulative) ===\n");
    long long raw_signals = 0, layer0 = 0, qualified = 0, approved = 0, rejected = 0;
    for (int i = 0; i < store.run_log.count; i++) {
        const RunLogEntry *e = vec_at(&store.run_log, i);
        raw_signals += e->signals_count;
        layer0 += e->layer0_passed;
        qualified += e->qualified;
        approved += e->bids_approved;
        rejected += e->bids_rejected;
    }
    printf("  %-16s: %lld\n", "raw_signals", raw_signals);
    printf("  %-16s: %lld\n", "layer0_passed", layer0);
    printf("  %-16s: %lld\n", "qualified", qualified);
    printf("  %-16s: %lld\n", "bids_approved", approved);
    printf("  %-16s: %lld\n", "bids_rejected", rejected);

    printf("\n=== Agent swarm ===\n");
    for (AgentLayer l = AL_L1_CENTRAL_COMMAND; l <= AL_SENTINEL; l++) {
        int count = 0;
        for (int i = 0; i < store.agents.count; i++) {
            const StoredAgent *a = vec_at(&store.agents, i);
            if (strcmp(a->layer, agent_layer_str(l)) == 0) count++;
        }
        if (count) printf("  %-24s: %d\n", agent_layer_str(l), count);
    }
    printf("  TOTAL AGENTS: %d\n", store.agents.count);

    int to_show = store.sentinel_events.count < 10 ? store.sentinel_events.count : 10;
    printf("\n=== Sentinel events (most recent %d) ===\n", to_show);
    for (int i = store.sentinel_events.count - 1, shown = 0; i >= 0 && shown < 10; i--, shown++) {
        const StoredSentinelEvent *e = vec_at(&store.sentinel_events, i);
        printf("  [%s] agent=%s drift=%.2f :: %s\n", e->event_type, e->agent_id, e->drift_score, e->reason);
    }

    printf("\n=== Strategy ledger ===\n");
    int ns = store.strategies.count;
    int *idx = ns ? malloc(sizeof(int) * (size_t) ns) : NULL;
    for (int i = 0; i < ns; i++) idx[i] = i;
    for (int i = 1; i < ns; i++) {
        int key = idx[i];
        const StoredStrategy *ka = vec_at(&store.strategies, key);
        int j = i - 1;
        while (j >= 0) {
            const StoredStrategy *ja = vec_at(&store.strategies, idx[j]);
            int c = strcmp(ja->domain, ka->domain);
            if (c == 0) c = strcmp(ja->name, ka->name);
            if (c <= 0) break;
            idx[j + 1] = idx[j];
            j--;
        }
        idx[j + 1] = key;
    }
    for (int i = 0; i < ns; i++) {
        const StoredStrategy *s = vec_at(&store.strategies, idx[i]);
        printf("  %-45s wins=%-3d losses=%-3d status=%s\n", s->strategy_id, s->wins, s->losses, s->status);
    }
    free(idx);

    printf("\n=== Sample qualified lead (full pipeline trace) ===\n");
    const StoredLead *sample = NULL;
    for (int i = store.leads.count - 1; i >= 0; i--) {
        const StoredLead *l = vec_at(&store.leads, i);
        if (strcmp(l->status, "BID_APPROVED") == 0) { sample = l; break; }
    }
    if (sample) {
        const StoredSignal *sig = NULL;
        for (int i = 0; i < store.signals.count; i++) {
            const StoredSignal *s = vec_at(&store.signals, i);
            if (strcmp(s->signal_id, sample->signal_id) == 0) { sig = s; break; }
        }
        const StoredBid *bid = NULL;
        for (int i = store.bids.count - 1; i >= 0; i--) {
            const StoredBid *b = vec_at(&store.bids, i);
            if (strcmp(b->lead_id, sample->lead_id) == 0) { bid = b; break; }
        }

        printf("  Platform     : %s / %s\n", sig ? sig->platform : "", (sig && sig->sub_domain[0]) ? sig->sub_domain : "-");
        printf("  Title        : %s\n", sig ? sig->title : "");
        printf("  Archetype    : %s\n", sample->client_archetype);
        printf("  Domain       : %s\n", sample->domain);
        char market_str[48], bid_str[48];
        format_money_commas(sample->market_price_usd, market_str, sizeof market_str);
        format_money_commas(sample->bid_price_usd, bid_str, sizeof bid_str);
        printf("  Market price : $%s\n", market_str);
        printf("  Bid price    : $%s  (margin %.1f%%)\n", bid_str, sample->margin_pct);
        if (bid) {
            printf("  Strategy     : %s (explore=%s)\n", bid->strategy_name, bid->explore ? "True" : "False");
            printf("  Reviewer     : %s\n", bid->reviewer_status);
            printf("  --- proposal ---\n%s\n\n", bid->proposal_text);
        }
    }

    printf("\nDB written to: %s\n", db_path);
    printf("Start the dashboard with:  ./iechm_server\n");
    return 0;
}
