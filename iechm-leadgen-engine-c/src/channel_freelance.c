/* Freelance & work-for-hire marketplaces -- mirrors core/channels/freelance.py. */
#include "channels.h"
#include "channels_util.h"
#include "strutil.h"
#include "fmt.h"
#include <stdio.h>

static const char *PLATFORMS[] = {"Upwork", "Freelancer.com", "Cad Crowd", "Guru"};
#define PLATFORMS_N 4

typedef struct { const char *title; double base_budget; } TitleDef;
static const TitleDef TITLES[] = {
    {"Need SolidWorks CAD model for a handheld device enclosure", 450},
    {"PCB layout for IoT sensor board (4-layer, Altium)", 900},
    {"Fusion 360 redesign of existing bracket for CNC machining", 250},
    {"DFM review + injection mold draft angles for plastic housing", 1200},
    {"KiCad schematic + Gerber files for a motor controller", 700},
    {"3D print prototype of a wearable clip, need STL + STEP", 180},
    {"Full NPD: napkin sketch to production-ready CAD for a kitchen gadget", 4500},
    {"Sheet metal enclosure design for server rack accessory", 650},
    {"Quick CAD conversion: hand sketch to STEP file", 60},
    {"Need help with tolerance stack-up analysis on an assembly", 300},
};
#define TITLES_N 10

static const char **fl_list_platforms(int *count) { *count = PLATFORMS_N; return PLATFORMS; }
static const char **fl_sub_domains(const char *platform, int *count) { (void) platform; *count = 0; return NULL; }
static const char *fl_friction_level(void) { return "LOW"; }

static void fl_generate(int day, Vec *out) {
    for (int p = 0; p < PLATFORMS_N; p++) {
        const char *platform = PLATFORMS[p];
        char salt[64];
        snprintf(salt, sizeof salt, "freelance:%s", platform);
        Rng r;
        channel_rng(&r, day, salt);
        int n = rng_randint(&r, 4, 9);
        for (int i = 0; i < n; i++) {
            char slug[32];
            snprintf(slug, sizeof slug, "job-%d-%d", day, i);

            if (rng_next_double(&r) < 0.08) {
                const char *text = BLACKLISTED_FILLER[rng_choice_index(&r, (size_t) BLACKLISTED_FILLER_N)];
                char title[TITLE_LEN];
                str_prefix(title, sizeof title, text, 60);
                static const double filler_budgets[] = {50, 100, 200};
                double budget = filler_budgets[rng_choice_index(&r, 3)];
                RawSignal s = make_signal(CT_FREELANCE, platform, "", title, text, true, budget, false, 0, slug);
                *(RawSignal *) vec_push(out) = s;
                continue;
            }

            const TitleDef *td = &TITLES[rng_choice_index(&r, TITLES_N)];
            double budget = round2(td->base_budget * rng_uniform(&r, 0.6, 1.6));

            char text[TEXT_LEN];
            snprintf(text, sizeof text, "%s. Budget range approx $%.0f. Looking for someone who can start this week.",
                     td->title, budget);
            maybe_inject_trap(&r, text, sizeof text, 0.12);

            RawSignal s = make_signal(CT_FREELANCE, platform, "", td->title, text, true, budget, false, 0, slug);
            *(RawSignal *) vec_push(out) = s;
        }
    }
}

static const ChannelAdapter ADAPTER = {
    .channel_type = CT_FREELANCE,
    .list_platforms = fl_list_platforms,
    .sub_domains = fl_sub_domains,
    .friction_level = fl_friction_level,
    .generate_daily_signals = fl_generate,
};

const ChannelAdapter *freelance_adapter(void) { return &ADAPTER; }
