/* Global B2B sourcing & trade directories -- mirrors core/channels/b2b_trade.py. */
#include "channels.h"
#include "channels_util.h"
#include "fmt.h"
#include <stdio.h>

static const char *PLATFORMS[] = {"Alibaba", "Made-in-China", "Global Sources", "IndiaMART"};
#define PLATFORMS_N 4

static const char *SUB_DOMAINS[] = {"RFQ:Tooling", "RFQ:PCBA", "RFQ:Sheet_Metal", "RFQ:Component_Sourcing"};
#define SUB_DOMAINS_N 4

typedef struct { const char *title; int volume; double base_budget; } RfqDef;
static const RfqDef RFQS[] = {
    {"Custom injection-molded ABS phone stand, need mold + DFM", 10000, 8500},
    {"OEM redesign of existing Bluetooth speaker housing with our logo", 5000, 6200},
    {"CNC machined aluminum heatsinks, need 2D drawings reviewed", 50000, 22000},
    {"Sheet metal enclosure for industrial control panel, 500 units", 500, 9000},
    {"PCB assembly (PCBA) for existing board design, 2000 units", 2000, 14000},
    {"Full tooling for plastic clip, private label packaging", 20000, 15000},
    {"Aluminum extrusion profile for LED fixture, custom length", 8000, 7000},
};
#define RFQS_N 7

static const char **b2b_list_platforms(int *count) { *count = PLATFORMS_N; return PLATFORMS; }
static const char **b2b_sub_domains(const char *platform, int *count) { (void) platform; *count = SUB_DOMAINS_N; return SUB_DOMAINS; }
static const char *b2b_friction_level(void) { return "HIGH"; }

static void b2b_generate(int day, Vec *out) {
    for (int p = 0; p < PLATFORMS_N; p++) {
        const char *platform = PLATFORMS[p];
        char salt[64];
        snprintf(salt, sizeof salt, "b2b:%s", platform);
        Rng r;
        channel_rng(&r, day, salt);
        int n = rng_randint(&r, 5, 12);
        for (int i = 0; i < n; i++) {
            const RfqDef *rf = &RFQS[rng_choice_index(&r, RFQS_N)];
            int volume = (int) (rf->volume * rng_uniform(&r, 0.5, 1.5));
            double budget = round2(rf->base_budget * rng_uniform(&r, 0.5, 1.4));
            if (rng_next_double(&r) < 0.1) budget = round2(volume * 0.005);

            char text[TEXT_LEN];
            snprintf(text, sizeof text,
                     "RFQ: %s. Target MOQ %d units. Budget ceiling $%.0f total. "
                     "Please quote unit price, lead time, and tooling cost separately.",
                     rf->title, volume, budget);
            maybe_inject_trap(&r, text, sizeof text, 0.05);

            const char *sub = SUB_DOMAINS[rng_choice_index(&r, SUB_DOMAINS_N)];

            char slug[32];
            snprintf(slug, sizeof slug, "rfq-%d-%d", day, i);
            RawSignal s = make_signal(CT_B2B_TRADE, platform, sub, rf->title, text, true, budget, true, volume, slug);
            *(RawSignal *) vec_push(out) = s;
        }
    }
}

static const ChannelAdapter ADAPTER = {
    .channel_type = CT_B2B_TRADE,
    .list_platforms = b2b_list_platforms,
    .sub_domains = b2b_sub_domains,
    .friction_level = b2b_friction_level,
    .generate_daily_signals = b2b_generate,
};

const ChannelAdapter *b2b_trade_adapter(void) { return &ADAPTER; }
