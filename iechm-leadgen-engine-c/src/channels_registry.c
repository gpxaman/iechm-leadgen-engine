#include "channels.h"

static const ChannelAdapter *REGISTRY[5];
static bool initialized = false;

static void ensure_init(void) {
    if (initialized) return;
    REGISTRY[0] = freelance_adapter();
    REGISTRY[1] = b2b_trade_adapter();
    REGISTRY[2] = community_adapter();
    REGISTRY[3] = brokerage_adapter();
    REGISTRY[4] = outbound_adapter();
    initialized = true;
}

int channels_count(void) { ensure_init(); return 5; }

const ChannelAdapter *channels_get(int index) {
    ensure_init();
    if (index < 0 || index >= 5) return NULL;
    return REGISTRY[index];
}

const ChannelAdapter *channels_find(ChannelType ct) {
    ensure_init();
    for (int i = 0; i < 5; i++) if (REGISTRY[i]->channel_type == ct) return REGISTRY[i];
    return NULL;
}
