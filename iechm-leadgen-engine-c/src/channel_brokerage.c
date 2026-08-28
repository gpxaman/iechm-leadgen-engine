/* Agency matchmaking & NPD brokerages -- mirrors core/channels/brokerage.py. */
#include "channels.h"
#include "channels_util.h"
#include "fmt.h"
#include <stdio.h>

static const char *PLATFORMS[] = {"Clutch.co", "DesignRush", "Gembah"};
#define PLATFORMS_N 3

typedef struct { const char *title; double base_budget; } RfpDef;
static const RfpDef RFPS[] = {
    {"Enterprise RFP: full mechanical + electrical engineering for IoT product line", 45000},
    {"Seeking vetted CAD/DFM partner for consumer appliance refresh, formal SOW required", 28000},
    {"Fractional PCB engineering team needed for 6-month embedded systems overflow", 60000},
    {"Full NPD partner for medtech-adjacent enclosure, NDA required before brief release", 90000},
    {"Sheet metal + tooling engineering RFP for industrial equipment manufacturer", 35000},
};
#define RFPS_N 5

static const char **bk_list_platforms(int *count) { *count = PLATFORMS_N; return PLATFORMS; }
static const char **bk_sub_domains(const char *platform, int *count) { (void) platform; *count = 0; return NULL; }
static const char *bk_friction_level(void) { return "HIGH"; } /* vetting/approval gated, not scraping-gated */

static void bk_generate(int day, Vec *out) {
    for (int p = 0; p < PLATFORMS_N; p++) {
        const char *platform = PLATFORMS[p];
        char salt[64];
        snprintf(salt, sizeof salt, "brokerage:%s", platform);
        Rng r;
        channel_rng(&r, day, salt);
        int n = rng_randint(&r, 0, 3); /* low volume, high value */
        for (int i = 0; i < n; i++) {
            const RfpDef *rf = &RFPS[rng_choice_index(&r, RFPS_N)];
            double budget = round2(rf->base_budget * rng_uniform(&r, 0.7, 1.5));

            char budget_str[48];
            format_number_commas(budget, 0, budget_str, sizeof budget_str);

            char text[TEXT_LEN];
            snprintf(text, sizeof text,
                     "%s. Formal RFP submitted via matchmaking portal. Budget band $%s. "
                     "Vetted partners only; case studies required with response.",
                     rf->title, budget_str);
            maybe_inject_trap(&r, text, sizeof text, 0.04);

            char slug[32];
            snprintf(slug, sizeof slug, "rfp-%d-%d", day, i);
            RawSignal s = make_signal(CT_BROKERAGE, platform, "", rf->title, text, true, budget, false, 0, slug);
            *(RawSignal *) vec_push(out) = s;
        }
    }
}

static const ChannelAdapter ADAPTER = {
    .channel_type = CT_BROKERAGE,
    .list_platforms = bk_list_platforms,
    .sub_domains = bk_sub_domains,
    .friction_level = bk_friction_level,
    .generate_daily_signals = bk_generate,
};

const ChannelAdapter *brokerage_adapter(void) { return &ADAPTER; }
