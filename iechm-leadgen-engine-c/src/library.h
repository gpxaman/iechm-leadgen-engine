/* core/library.py port -- THE public API. http.c only ever calls into this
 * file; it in turn is the only place that touches both the Store and the
 * Orchestrator, exactly mirroring the seam the original draws around
 * core.library so a different frontend (or transport) could replace
 * http.c/main.c without touching anything else. Every function returns a
 * malloc'd, NUL-terminated JSON string that the caller must free(); NULL
 * means "not found" where that's a meaningful outcome (library_get_lead). */
#ifndef IECHM_LIBRARY_H
#define IECHM_LIBRARY_H

#include <stddef.h>
#include <stdbool.h>

void library_init(const char *db_path);
void library_reset_all(void);

/* has_day=false advances the internal day counter automatically (what the
 * UI's "Run Next Cycle" button does); has_day=true pins it to `day`. */
char *library_run_cycle(bool has_day, int day);

int library_current_day(void);

char *library_list_leads(int limit, const char *status_or_null, const char *channel_type_or_null);
char *library_get_lead(const char *lead_id); /* NULL if not found */
char *library_list_agents(void);
char *library_list_sentinel_events(int limit);
char *library_list_strategies(void);
char *library_funnel_metrics(void);
char *library_list_channel_types(void);

#endif
