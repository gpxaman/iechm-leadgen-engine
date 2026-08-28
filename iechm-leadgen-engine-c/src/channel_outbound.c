/* Outbound signals: crowdfunding campaigns and patent filings -- mirrors
 * core/channels/outbound.py. No stated budget on purpose: intent is
 * inferred from funding/patent activity, not a stated ask. */
#include "channels.h"
#include "channels_util.h"
#include "strutil.h"
#include <stdio.h>
#include <string.h>

static const char *PLATFORMS[] = {"Kickstarter", "Indiegogo", "USPTO Patent Filings"};
#define PLATFORMS_N 3

static const char *CROWDFUND_SUBS[] = {"Technology", "Design", "Gadgets"};
static const char *PATENT_SUBS[] = {"Utility Patents", "Design Patents"};

static const char *SIGNAL_TEMPLATES[] = {
    "Funded 140% on our smart-home hub campaign, but our CAD is still just a 3D render, no DFM done yet, worried about our July ship date.",
    "Update #4: manufacturing delay, our injection mold supplier rejected our files for zero draft angle, need to redesign fast.",
    "New utility patent published for a modular enclosure system, no visible commercial manufacturing partner yet.",
    "Hit our Indiegogo goal for a wearable device, need to go from prototype PCB to production-ready Gerber files.",
    "Patent filing describes a novel sheet-metal bracket mechanism, inventor appears to be a first-time hardware founder.",
};
#define SIGNAL_TEMPLATES_N 5

static bool is_crowdfund_platform(const char *platform) {
    return strcmp(platform, "Kickstarter") == 0 || strcmp(platform, "Indiegogo") == 0;
}

static const char **ob_list_platforms(int *count) { *count = PLATFORMS_N; return PLATFORMS; }

static const char **ob_sub_domains(const char *platform, int *count) {
    if (is_crowdfund_platform(platform)) { *count = 3; return CROWDFUND_SUBS; }
    *count = 2;
    return PATENT_SUBS;
}

static const char *ob_friction_level(void) { return "MEDIUM"; }

static void ob_generate(int day, Vec *out) {
    for (int p = 0; p < PLATFORMS_N; p++) {
        const char *platform = PLATFORMS[p];
        char salt[64];
        snprintf(salt, sizeof salt, "outbound:%s", platform);
        Rng r;
        channel_rng(&r, day, salt);
        int n = rng_randint(&r, 2, 6);

        int sub_count;
        const char **subs = ob_sub_domains(platform, &sub_count);

        for (int i = 0; i < n; i++) {
            const char *tmpl = SIGNAL_TEMPLATES[rng_choice_index(&r, SIGNAL_TEMPLATES_N)];
            char text[TEXT_LEN];
            xcpy(text, sizeof text, tmpl);
            maybe_inject_trap(&r, text, sizeof text, 0.02);

            const char *sub = subs[rng_choice_index(&r, (size_t) sub_count)];

            char title[TITLE_LEN];
            str_prefix(title, sizeof title, text, 65);

            char slug[32];
            snprintf(slug, sizeof slug, "signal-%d-%d", day, i);
            RawSignal s = make_signal(CT_OUTBOUND, platform, sub, title, text, false, 0.0, false, 0, slug);
            *(RawSignal *) vec_push(out) = s;
        }
    }
}

static const ChannelAdapter ADAPTER = {
    .channel_type = CT_OUTBOUND,
    .list_platforms = ob_list_platforms,
    .sub_domains = ob_sub_domains,
    .friction_level = ob_friction_level,
    .generate_daily_signals = ob_generate,
};

const ChannelAdapter *outbound_adapter(void) { return &ADAPTER; }
