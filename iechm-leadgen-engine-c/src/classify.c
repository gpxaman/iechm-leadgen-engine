#include "classify.h"
#include "strutil.h"
#include <stdio.h>

/* Sorted (Python `sorted()` over a set, ASCII order) so extraction order
 * matches the original without needing a separate sort/dedup step here --
 * each list has no duplicate entries, so appending in this fixed order
 * already yields the deduped, sorted result Python produces. */
static const char *CAD_TOOLS_SORTED[] = {
    "Altium", "AutoCAD", "CATIA", "Creo", "Fusion 360", "IGES", "KiCad", "STEP", "SolidWorks",
};
#define CAD_TOOLS_N 9

static const char *MATERIALS_SORTED[] = {
    "ABS", "PC", "PLA", "aluminium", "aluminum", "brass", "nylon", "stainless steel", "steel", "titanium",
};
#define MATERIALS_N 10

void extract_cad_software(const char *text, char out[LIST_MAX][LIST_ITEM_LEN], int *out_count) {
    int n = 0;
    for (int i = 0; i < CAD_TOOLS_N && n < LIST_MAX; i++) {
        if (has_plain(text, CAD_TOOLS_SORTED[i])) xcpy(out[n++], LIST_ITEM_LEN, CAD_TOOLS_SORTED[i]);
    }
    *out_count = n;
}

void extract_materials(const char *text, char out[LIST_MAX][LIST_ITEM_LEN], int *out_count) {
    int n = 0;
    for (int i = 0; i < MATERIALS_N && n < LIST_MAX; i++) {
        if (has_plain(text, MATERIALS_SORTED[i])) xcpy(out[n++], LIST_ITEM_LEN, MATERIALS_SORTED[i]);
    }
    *out_count = n;
}

void extract_deliverables(const char *text, char out[LIST_MAX][LIST_ITEM_LEN], int *out_count) {
    int n = 0;
    if (has_prefix_word(text, "step file") && n < LIST_MAX) xcpy(out[n++], LIST_ITEM_LEN, "STEP file");
    if (has_word(text, "gerber") && n < LIST_MAX) xcpy(out[n++], LIST_ITEM_LEN, "Gerber/BOM");
    if ((has_plain(text, "2d drawing") || has_plain(text, "2d production drawing")) && n < LIST_MAX)
        xcpy(out[n++], LIST_ITEM_LEN, "2D Production Drawings");
    if (has_word(text, "bom") && n < LIST_MAX) xcpy(out[n++], LIST_ITEM_LEN, "BOM");
    if (has_plain(text, "render") && n < LIST_MAX) xcpy(out[n++], LIST_ITEM_LEN, "Renders");
    if (has_plain(text, "moldflow") && n < LIST_MAX) xcpy(out[n++], LIST_ITEM_LEN, "Moldflow Analysis");
    if (has_plain(text, "prototype") && n < LIST_MAX) xcpy(out[n++], LIST_ITEM_LEN, "Physical Prototype");

    if (n == 0) {
        xcpy(out[0], LIST_ITEM_LEN, "STEP file");
        xcpy(out[1], LIST_ITEM_LEN, "2D Production Drawings");
        n = 2;
    }
    *out_count = n;
}

const char *classify_project_stage(const char *text) {
    if (has_plain(text, "napkin sketch") || has_plain(text, "just an idea") ||
        has_plain(text, "concept only") || has_plain(text, "early concept"))
        return "Concept / Napkin Sketch";

    if (has_plain(text, "working prototype") || has_plain(text, "proof of concept") || has_word(text, "poc"))
        return "Prototype / Proof of Concept";

    if (has_plain(text, "ready for production") || has_plain(text, "production ready") ||
        has_plain(text, "need dfm") || has_plain(text, "scale to production"))
        return "Optimization / DFM";

    if (has_plain(text, "mass production") || has_plain(text, "high volume") ||
        has_plain(text, "going into production"))
        return "Production Ready";

    return "Unspecified";
}

ManufacturingDomain classify_domain(const char *text) {
    if (has_word(text, "pcb") || has_word(text, "altium") || has_word(text, "kicad") ||
        has_word(text, "gerber") || has_word(text, "schematic") || has_word(text, "firmware") ||
        has_word(text, "embedded"))
        return DOM_PCB_ELECTRONICS;

    if (has_plain(text, "injection mold") || has_word(text, "dfm") || has_plain(text, "draft angle") ||
        has_plain(text, "mold flow") || has_plain(text, "tooling"))
        return DOM_DFM_INJECTION_MOLDING;

    if (has_plain(text, "sheet metal") || has_plain(text, "stamping") || has_plain(text, "bending") ||
        has_plain(text, "extrusion"))
        return DOM_SHEET_METAL;

    if (has_plain(text, "3d print") || has_word(text, "sla") || has_word(text, "fdm") ||
        has_word(text, "sls") || has_plain(text, "rapid prototyp"))
        return DOM_PROTOTYPING_3D_PRINT;

    if (has_plain(text, "enclosure") || has_plain(text, "housing") || has_plain(text, "casing"))
        return DOM_ENCLOSURE_DESIGN;

    if (has_plain(text, "turnkey") || has_plain(text, "napkin sketch") ||
        has_plain(text, "concept to production") || has_plain(text, "full product development"))
        return DOM_FULL_NPD_TURNKEY;

    if (has_word(text, "cad") || has_plain(text, "solidworks") ||
        has_plain(text, "fusion360") || has_plain(text, "fusion 360") ||
        has_plain(text, "mechanical design") || has_prefix_word(text, "step file") || has_word(text, "iges"))
        return DOM_CAD_MECHANICAL;

    return DOM_UNKNOWN;
}

static bool has_white_label(const char *text) {
    int pos = ci_find(text, "white", 0);
    while (pos >= 0) {
        int after = pos + 5;
        if (ci_find(text, "label", after) == after) return true;
        if (text[after] != '\0' && ci_find(text, "label", after + 1) == after + 1) return true;
        pos = ci_find(text, "white", pos + 1);
    }
    return false;
}

static ClientArchetype archetype_override(const char *text) {
    if (has_plain(text, "government") || has_plain(text, "tender") || has_suffix_word(text, "rfp") ||
        has_plain(text, "defense") || has_plain(text, "public sector") || has_plain(text, "procurement"))
        return ARCH_INSTITUTIONAL;

    if (has_plain(text, "reseller") || has_white_label(text) || has_plain(text, "oem") ||
        has_plain(text, "odm") || has_plain(text, "import") ||
        has_plain(text, "amazon fba") || has_plain(text, "amazon seller") || has_plain(text, "shopify brand"))
        return ARCH_MIDDLEMAN_RESELLER;

    if (has_plain(text, "kickstarter") || has_plain(text, "indiegogo") ||
        has_plain(text, "crowdfund") || has_plain(text, "our backers"))
        return ARCH_CROWDFUNDER;

    if (has_plain(text, "engineering team") || has_plain(text, "internal team") ||
        has_plain(text, "overflow") || has_plain(text, "bandwidth") || has_plain(text, "fractional engineer"))
        return ARCH_SME_ENGINEERING;

    return ARCH_UNKNOWN; /* sentinel meaning "no override matched" */
}

static ClientArchetype archetype_by_channel(ChannelType ct) {
    switch (ct) {
        case CT_FREELANCE: return ARCH_NPD_INNOVATOR;
        case CT_B2B_TRADE: return ARCH_MIDDLEMAN_RESELLER;
        case CT_COMMUNITY: return ARCH_NPD_INNOVATOR;
        case CT_BROKERAGE: return ARCH_SME_ENGINEERING;
        case CT_OUTBOUND: return ARCH_CROWDFUNDER;
        default: return ARCH_UNKNOWN;
    }
}

ClientArchetype classify_archetype(const RawSignal *signal) {
    char text[TITLE_LEN + TEXT_LEN + 2];
    snprintf(text, sizeof text, "%s\n%s", signal->title, signal->raw_text);

    ClientArchetype override = archetype_override(text);
    if (override != ARCH_UNKNOWN) return override;
    return archetype_by_channel(signal->channel_type);
}
