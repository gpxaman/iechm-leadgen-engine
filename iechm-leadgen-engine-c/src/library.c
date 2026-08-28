#include "library.h"
#include "store.h"
#include "orchestrator.h"
#include "channels.h"
#include "jsonw.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

typedef struct {
    Store store;
    Orchestrator orch;
    int current_day;
    char db_path[512];
    bool initialized;
} LibraryState;

static LibraryState g_lib;

/* http.c hands each connection to its own thread; unlike the original,
 * which reads always go through a freshly opened, thread-owned sqlite
 * connection (only concurrent writes to the shared Orchestrator were ever
 * at risk there), every read here touches the SAME in-memory Store/
 * Orchestrator a concurrent POST /api/run-cycle could be mutating --
 * including growing a Vec's backing array via realloc, which would leave a
 * concurrent reader holding a dangling pointer. One coarse mutex around
 * every library_* call trades parallelism (irrelevant at this request
 * volume) for ruling that out entirely. */
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

void library_init(const char *db_path) {
    pthread_mutex_lock(&g_mutex);
    xcpy(g_lib.db_path, sizeof g_lib.db_path, db_path);
    store_init(&g_lib.store);
    store_load_from_disk(&g_lib.store, db_path); /* fine if it doesn't exist yet */
    orchestrator_init(&g_lib.orch);
    g_lib.current_day = 0;
    g_lib.initialized = true;
    pthread_mutex_unlock(&g_mutex);
}

void library_reset_all(void) {
    pthread_mutex_lock(&g_mutex);
    store_reset(&g_lib.store);
    orchestrator_init(&g_lib.orch);
    g_lib.current_day = 0;
    store_save_to_disk(&g_lib.store, g_lib.db_path);
    pthread_mutex_unlock(&g_mutex);
}

char *library_run_cycle(bool has_day, int day) {
    pthread_mutex_lock(&g_mutex);

    if (!has_day) {
        g_lib.current_day += 1;
        day = g_lib.current_day;
    } else if (day > g_lib.current_day) {
        g_lib.current_day = day;
    }

    CycleSummary summary;
    orchestrator_run_cycle(&g_lib.orch, &g_lib.store, day, &summary);
    store_save_to_disk(&g_lib.store, g_lib.db_path);

    Jsonw w;
    jw_init(&w);
    jw_obj_open(&w);
    jw_kv_int(&w, "day", summary.day);
    jw_kv_int(&w, "total_signals", summary.total_signals);
    jw_kv_int(&w, "total_qualified", summary.total_qualified);
    jw_kv_int(&w, "total_bids_approved", summary.total_bids_approved);
    jw_kv_int(&w, "sentinel_events_this_cycle", summary.sentinel_events_this_cycle);
    jw_key(&w, "channels");
    jw_arr_open(&w);
    for (int i = 0; i < summary.channels_count; i++) {
        const ChannelCycleSummary *c = &summary.channels[i];
        jw_obj_open(&w);
        jw_kv_str(&w, "channel_type", c->channel_type);
        jw_kv_int(&w, "platforms", c->platforms);
        jw_kv_int(&w, "signals", c->signals);
        jw_kv_int(&w, "layer0_passed", c->layer0_passed);
        jw_kv_int(&w, "qualified", c->qualified);
        jw_kv_int(&w, "bids_approved", c->bids_approved);
        jw_kv_int(&w, "bids_rejected", c->bids_rejected);
        jw_key(&w, "subdomain_agents_spawned");
        jw_arr_open(&w);
        for (int j = 0; j < c->spawned_count; j++) jw_str(&w, c->spawned[j]);
        jw_arr_close(&w);
        jw_key(&w, "subdomain_agents_deprecated");
        jw_arr_open(&w);
        for (int j = 0; j < c->deprecated_count; j++) jw_str(&w, c->deprecated[j]);
        jw_arr_close(&w);
        jw_kv_dbl(&w, "channel_health_score", c->channel_health_score, 3);
        jw_obj_close(&w);
    }
    jw_arr_close(&w);
    jw_obj_close(&w);

    pthread_mutex_unlock(&g_mutex);
    return jw_finish(&w);
}

int library_current_day(void) {
    pthread_mutex_lock(&g_mutex);
    int d = g_lib.current_day;
    pthread_mutex_unlock(&g_mutex);
    return d;
}

char *library_list_leads(int limit, const char *status_or_null, const char *channel_type_or_null) {
    pthread_mutex_lock(&g_mutex);
    Jsonw w;
    jw_init(&w);
    store_write_leads_json(&g_lib.store, &w, limit, status_or_null, channel_type_or_null);
    pthread_mutex_unlock(&g_mutex);
    return jw_finish(&w);
}

char *library_get_lead(const char *lead_id) {
    pthread_mutex_lock(&g_mutex);
    Jsonw w;
    jw_init(&w);
    bool found = store_write_lead_detail_json(&g_lib.store, &w, lead_id);
    pthread_mutex_unlock(&g_mutex);
    if (!found) {
        char *discard = jw_finish(&w);
        free(discard);
        return NULL;
    }
    return jw_finish(&w);
}

char *library_list_agents(void) {
    pthread_mutex_lock(&g_mutex);
    Jsonw w;
    jw_init(&w);
    store_write_agents_json(&g_lib.store, &w);
    pthread_mutex_unlock(&g_mutex);
    return jw_finish(&w);
}

char *library_list_sentinel_events(int limit) {
    pthread_mutex_lock(&g_mutex);
    Jsonw w;
    jw_init(&w);
    store_write_sentinel_events_json(&g_lib.store, &w, limit);
    pthread_mutex_unlock(&g_mutex);
    return jw_finish(&w);
}

char *library_list_strategies(void) {
    pthread_mutex_lock(&g_mutex);
    Jsonw w;
    jw_init(&w);
    store_write_strategies_json(&g_lib.store, &w);
    pthread_mutex_unlock(&g_mutex);
    return jw_finish(&w);
}

char *library_funnel_metrics(void) {
    pthread_mutex_lock(&g_mutex);
    Jsonw w;
    jw_init(&w);
    store_write_funnel_json(&g_lib.store, &w);
    pthread_mutex_unlock(&g_mutex);
    return jw_finish(&w);
}

/* Built fresh from the channel registry, not the Store -- no locking needed. */
char *library_list_channel_types(void) {
    Jsonw w;
    jw_init(&w);
    jw_arr_open(&w);
    for (int i = 0; i < channels_count(); i++) {
        const ChannelAdapter *a = channels_get(i);
        int platform_count;
        const char **platforms = a->list_platforms(&platform_count);

        jw_obj_open(&w);
        jw_kv_str(&w, "channel_type", channel_type_str(a->channel_type));
        jw_key(&w, "platforms");
        jw_arr_open(&w);
        for (int p = 0; p < platform_count; p++) jw_str(&w, platforms[p]);
        jw_arr_close(&w);

        jw_key(&w, "sub_domains");
        jw_obj_open(&w);
        for (int p = 0; p < platform_count; p++) {
            int sub_count;
            const char **subs = a->sub_domains(platforms[p], &sub_count);
            if (sub_count == 0) continue;
            jw_key(&w, platforms[p]);
            jw_arr_open(&w);
            for (int s = 0; s < sub_count; s++) jw_str(&w, subs[s]);
            jw_arr_close(&w);
        }
        jw_obj_close(&w);

        jw_kv_str(&w, "friction_level", a->friction_level());
        jw_obj_close(&w);
    }
    jw_arr_close(&w);
    return jw_finish(&w);
}
