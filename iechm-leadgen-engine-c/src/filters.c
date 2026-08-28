#include "filters.h"
#include "strutil.h"
#include <stdio.h>

#define COMBINED_LEN (TITLE_LEN + TEXT_LEN + 2)

static const double MIN_VIABLE_BUDGET_USD = 40.0;

static bool blacklisted(const char *text) {
    if (has_word(text, "fabric")) return true;
    if (has_prefix_word(text, "textile")) return true;
    if (has_word(text, "apparel")) return true;
    if (has_prefix_word(text, "garment")) return true;
    if (has_word(text, "clothing")) return true;
    if (has_word(text, "food")) return true;
    if (has_prefix_word(text, "agricultur")) return true;
    if (has_word(text, "wheat")) return true;
    if (has_prefix_word(text, "fertiliz")) return true;
    if (has_word(text, "chemical formulation")) return true;
    if (has_word(text, "pharmaceutical")) return true;
    if (has_prefix_word(text, "cosmetic")) return true;
    if (has_prefix_word(text, "content writ")) return true;
    if (has_prefix_word(text, "copywrit")) return true;
    if (has_word(text, "seo")) return true;
    if (has_word(text, "wordpress")) return true;
    if (has_word(text, "mobile app")) return true;
    if (has_word(text, "website design")) return true;
    if (has_word(text, "logo design only")) return true;
    return false;
}

static bool has_hardware_markers(const char *text) {
    if (has_word(text, "cad")) return true;
    if (has_word(text, "step")) return true;
    if (has_word(text, "stl")) return true;
    if (has_word(text, "iges")) return true;
    if (has_word(text, "pcb")) return true;
    if (has_word(text, "gerber")) return true;
    if (has_word(text, "altium")) return true;
    if (has_word(text, "kicad")) return true;
    if (has_word(text, "fusion360") || has_word(text, "fusion 360")) return true;
    if (has_word(text, "solidworks")) return true;
    if (has_word(text, "cnc")) return true;
    if (has_prefix_word(text, "injection mold")) return true;
    if (has_word(text, "sheet metal")) return true;
    if (has_word(text, "dfm")) return true;
    if (has_word(text, "dfa")) return true;
    if (has_word(text, "bom")) return true;
    if (has_word(text, "enclosure")) return true;
    if (has_prefix_word(text, "3d print")) return true;
    if (has_prefix_word(text, "prototyp")) return true;
    if (has_word(text, "tooling")) return true;
    if (has_word(text, "aluminum")) return true;
    if (has_word(text, "aluminium")) return true;
    if (has_word(text, "mechanical design")) return true;
    if (has_word(text, "firmware")) return true;
    if (has_word(text, "embedded")) return true;
    if (has_word(text, "rfq")) return true;
    if (has_word(text, "moq")) return true;
    if (has_prefix_word(text, "manufactur")) return true;
    return false;
}

Layer0Result passes_layer0(const RawSignal *signal) {
    char text[COMBINED_LEN];
    snprintf(text, sizeof text, "%s\n%s", signal->title, signal->raw_text);

    if (blacklisted(text)) return (Layer0Result){false, "blacklisted_category"};
    if (!has_hardware_markers(text)) return (Layer0Result){false, "no_hardware_markers"};

    if (signal->has_budget) {
        if (signal->stated_budget_usd < MIN_VIABLE_BUDGET_USD)
            return (Layer0Result){false, "budget_below_floor"};

        if (signal->has_volume && signal->target_volume > 0) {
            double per_unit = signal->stated_budget_usd / (double) signal->target_volume;
            if (per_unit < 0.02 && signal->target_volume >= 1000)
                return (Layer0Result){false, "budget_mathematically_impossible"};
        }
    }

    return (Layer0Result){true, "ok"};
}
