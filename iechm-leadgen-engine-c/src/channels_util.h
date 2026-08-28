/* Shared helpers for the mock channel-signal generators, mirroring
 * core/channels/_util.py. Not part of the adapter contract itself. */
#ifndef IECHM_CHANNELS_UTIL_H
#define IECHM_CHANNELS_UTIL_H

#include "models.h"
#include "rng.h"

/* Rng seeded exactly like Python's `random.Random(f"{day}:{salt}")`. */
void channel_rng(Rng *r, int day, const char *salt);

extern const char *BLACKLISTED_FILLER[];
extern const int BLACKLISTED_FILLER_N;

RawSignal make_signal(ChannelType channel_type, const char *platform, const char *sub_domain,
                       const char *title, const char *raw_text, bool has_budget, double budget,
                       bool has_volume, int volume, const char *url_slug);

/* Appends an anti-bot phrase and/or a prompt-injection phrase onto `text`
 * (in place, bounds-checked) with the given probabilities, exactly as
 * `_util.maybe_inject_trap` does. */
void maybe_inject_trap(Rng *r, char *text, size_t cap, double trap_rate);

#endif
