/* Developer/community forums -- mirrors core/channels/community.py. This is
 * the channel where sub-domain volume is deliberately uneven, so the
 * 5-leads/day spawn rule in the orchestrator actually has something to
 * fire on. */
#include "channels.h"
#include "channels_util.h"
#include "strutil.h"
#include "fmt.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static const char *PLATFORMS[] = {"Reddit", "Hackaday", "Discord (Hardware Servers)"};
#define PLATFORMS_N 3

static const char *REDDIT_NAMES[] = {"r/HardwareStartups", "r/PrintedCircuitBoard", "r/CAD", "r/Machinists", "r/injectionmolding", "r/AskElectronics"};
static const int REDDIT_AVGS[] = {6, 8, 3, 2, 4, 7};
#define REDDIT_N 6

static const char *HACKADAY_NAMES[] = {"io.hackaday.com/projects"};
static const int HACKADAY_AVGS[] = {3};
#define HACKADAY_N 1

static const char *DISCORD_NAMES[] = {"#hardware-help", "#pcb-review"};
static const int DISCORD_AVGS[] = {4, 2};
#define DISCORD_N 2

static const char *POST_TEMPLATES[] = {
    "Just got my first 3D printed prototype back and the walls are way too thin, how do I fix draft angles for injection molding later?",
    "Posting my PCB layout (KiCad) for review before I send it to fab, any glaring EMC issues?",
    "We hit our Kickstarter goal, now realizing our CAD isn't manufacturable at all. Where do people usually go for DFM help?",
    "CNC quote came back 3x higher than expected for this bracket, is my design just bad for machining?",
    "Trying to go from SolidWorks model to actual injection mold, no idea where to start with tooling cost.",
    "Anyone recommend someone who does full turnkey NPD? Have a napkin sketch and a bit of funding.",
};
#define POST_TEMPLATES_N 6

static bool get_subs(const char *platform, const char ***names, const int **avgs, int *n) {
    if (strcmp(platform, "Reddit") == 0) { *names = (const char **) REDDIT_NAMES; *avgs = REDDIT_AVGS; *n = REDDIT_N; return true; }
    if (strcmp(platform, "Hackaday") == 0) { *names = (const char **) HACKADAY_NAMES; *avgs = HACKADAY_AVGS; *n = HACKADAY_N; return true; }
    if (strcmp(platform, "Discord (Hardware Servers)") == 0) { *names = (const char **) DISCORD_NAMES; *avgs = DISCORD_AVGS; *n = DISCORD_N; return true; }
    *names = NULL;
    *avgs = NULL;
    *n = 0;
    return false;
}

static const char **cm_list_platforms(int *count) { *count = PLATFORMS_N; return PLATFORMS; }

static const char **cm_sub_domains(const char *platform, int *count) {
    const char **names = NULL; const int *avgs; int n;
    get_subs(platform, &names, &avgs, &n);
    *count = n;
    return names;
}

static const char *cm_friction_level(void) { return "LOW"; }

static void make_slug(char *dst, size_t cap, int day, const char *sub_domain, int i) {
    char raw[SHORT_LEN + 32];
    snprintf(raw, sizeof raw, "post-%d-%s-%d", day, sub_domain, i);
    size_t w = 0;
    for (const char *p = raw; *p && w + 1 < cap; p++) {
        if (*p == '#') continue;
        dst[w++] = (*p == '/') ? '_' : *p;
    }
    dst[w] = '\0';
}

static void cm_generate(int day, Vec *out) {
    for (int p = 0; p < PLATFORMS_N; p++) {
        const char *platform = PLATFORMS[p];
        const char **names; const int *avgs; int nsubs;
        get_subs(platform, &names, &avgs, &nsubs);

        for (int si = 0; si < nsubs; si++) {
            const char *sub_domain = names[si];
            int avg = avgs[si];

            char salt[128];
            snprintf(salt, sizeof salt, "community:%s:%s", platform, sub_domain);
            Rng r;
            channel_rng(&r, day, salt);

            int n = (int) round((double) avg + rng_uniform(&r, -2.0, 3.0));
            if (n < 0) n = 0;

            for (int i = 0; i < n; i++) {
                const char *tmpl = POST_TEMPLATES[rng_choice_index(&r, POST_TEMPLATES_N)];
                char text[TEXT_LEN];
                xcpy(text, sizeof text, tmpl);
                maybe_inject_trap(&r, text, sizeof text, 0.03);

                double roll = rng_next_double(&r);
                bool has_budget = !(roll < 0.7);
                double budget = has_budget ? round2(rng_uniform(&r, 100.0, 2000.0)) : 0.0;

                char title[TITLE_LEN];
                str_prefix(title, sizeof title, text, 70);

                char slug[SHORT_LEN + 32];
                make_slug(slug, sizeof slug, day, sub_domain, i);

                RawSignal s = make_signal(CT_COMMUNITY, platform, sub_domain, title, text, has_budget, budget, false, 0, slug);
                *(RawSignal *) vec_push(out) = s;
            }
        }
    }
}

static const ChannelAdapter ADAPTER = {
    .channel_type = CT_COMMUNITY,
    .list_platforms = cm_list_platforms,
    .sub_domains = cm_sub_domains,
    .friction_level = cm_friction_level,
    .generate_daily_signals = cm_generate,
};

const ChannelAdapter *community_adapter(void) { return &ADAPTER; }
