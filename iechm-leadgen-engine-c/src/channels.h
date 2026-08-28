/* Channel adapter contract -- mirrors core/channels/base.py. Every lead
 * source type implements the same three operations; the orchestrator never
 * knows which concrete channel it's driving. `generate_daily_signals` is a
 * SIMULATOR standing in for a real scraper, exactly as in the original. */
#ifndef IECHM_CHANNELS_H
#define IECHM_CHANNELS_H

#include "models.h"
#include "vec.h"

typedef struct {
    ChannelType channel_type;
    const char **(*list_platforms)(int *count);
    const char **(*sub_domains)(const char *platform, int *count); /* count=0, may return NULL */
    const char *(*friction_level)(void); /* "LOW" | "MEDIUM" | "HIGH" */
    void (*generate_daily_signals)(int day, Vec *out); /* appends RawSignal to out (elem_size == sizeof(RawSignal)) */
} ChannelAdapter;

const ChannelAdapter *freelance_adapter(void);
const ChannelAdapter *b2b_trade_adapter(void);
const ChannelAdapter *community_adapter(void);
const ChannelAdapter *brokerage_adapter(void);
const ChannelAdapter *outbound_adapter(void);

int channels_count(void);
const ChannelAdapter *channels_get(int index);
const ChannelAdapter *channels_find(ChannelType ct);

#endif
