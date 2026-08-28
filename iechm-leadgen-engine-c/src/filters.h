/* Layer 0 -- deterministic pre-filter. Mirrors core/filters.py exactly:
 * plain term matching and arithmetic, no model call, runs before anything
 * else ever sees a raw signal. */
#ifndef IECHM_FILTERS_H
#define IECHM_FILTERS_H

#include "models.h"

typedef struct {
    bool passed;
    const char *reason; /* static string literal, never freed */
} Layer0Result;

Layer0Result passes_layer0(const RawSignal *signal);

#endif
