#include "models.h"
#include "jsonw.h"
#include "rng.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

const char *channel_type_str(ChannelType t) {
    switch (t) {
        case CT_FREELANCE: return "FREELANCE_MARKETPLACE";
        case CT_B2B_TRADE: return "B2B_TRADE_DIRECTORY";
        case CT_COMMUNITY: return "COMMUNITY_FORUM";
        case CT_BROKERAGE: return "AGENCY_BROKERAGE";
        case CT_OUTBOUND: return "OUTBOUND_SIGNAL";
        default: return "";
    }
}

const char *archetype_str(ClientArchetype a) {
    switch (a) {
        case ARCH_NPD_INNOVATOR: return "NPD_INNOVATOR";
        case ARCH_MIDDLEMAN_RESELLER: return "MIDDLEMAN_RESELLER_OEM_ODM";
        case ARCH_SME_ENGINEERING: return "SME_ENGINEERING_OVERFLOW";
        case ARCH_CROWDFUNDER: return "CROWDFUNDER_FUNDED";
        case ARCH_INSTITUTIONAL: return "INSTITUTIONAL_PUBLIC_SECTOR";
        case ARCH_UNKNOWN: default: return "UNKNOWN";
    }
}

const char *domain_str(ManufacturingDomain d) {
    switch (d) {
        case DOM_CAD_MECHANICAL: return "cad_mechanical";
        case DOM_PCB_ELECTRONICS: return "pcb_electronics";
        case DOM_ENCLOSURE_DESIGN: return "enclosure_design";
        case DOM_PROTOTYPING_3D_PRINT: return "prototyping_3d_print";
        case DOM_DFM_INJECTION_MOLDING: return "dfm_injection_molding";
        case DOM_SHEET_METAL: return "sheet_metal";
        case DOM_FULL_NPD_TURNKEY: return "full_npd_turnkey";
        case DOM_UNKNOWN: default: return "unknown";
    }
}

const char *lead_status_str(LeadStatus s) {
    switch (s) {
        case LS_REJECTED_LAYER0: return "REJECTED_LAYER0";
        case LS_REJECTED_QUALIFICATION: return "REJECTED_QUALIFICATION";
        case LS_QUALIFIED: return "QUALIFIED";
        case LS_BID_DRAFTED: return "BID_DRAFTED";
        case LS_BID_APPROVED: return "BID_APPROVED";
        case LS_BID_REJECTED_BY_REVIEWER: default: return "BID_REJECTED_BY_REVIEWER";
    }
}

const char *agent_status_str(AgentStatus s) {
    switch (s) {
        case AS_ACTIVE: return "ACTIVE";
        case AS_IDLE: return "IDLE";
        case AS_QUARANTINED: return "QUARANTINED";
        case AS_DEPRECATED: default: return "DEPRECATED";
    }
}

const char *agent_layer_str(AgentLayer l) {
    switch (l) {
        case AL_L1_CENTRAL_COMMAND: return "L1_CENTRAL_COMMAND";
        case AL_L2_MACRO_CHANNEL: return "L2_MACRO_CHANNEL";
        case AL_L3_PLATFORM_WORKER: return "L3_PLATFORM_WORKER";
        case AL_L4_SUBDOMAIN_WORKER: return "L4_SUBDOMAIN_WORKER";
        case AL_SENTINEL: default: return "SENTINEL";
    }
}

void new_id(char *dst, size_t cap, const char *prefix) {
    uint64_t a = rng_next_u64(global_rng());
    uint64_t b = rng_next_u64(global_rng());
    char buf[ID_LEN];
    snprintf(buf, sizeof buf, "%s_%08llx%08llx", prefix,
              (unsigned long long) (a & 0xffffffffULL),
              (unsigned long long) (b & 0xffffffffULL));
    xcpy(dst, cap, buf);
}

void now_iso(char *dst, size_t cap) {
    time_t t = time(NULL);
    struct tm tmv;
    gmtime_r(&t, &tmv);
    char buf[64];
    strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tmv);
    xcpy(dst, cap, buf);
}

void trap_analysis_to_json(const TrapAnalysis *t, char *dst, size_t cap) {
    Jsonw w;
    jw_init(&w);
    jw_obj_open(&w);
    jw_kv_bool(&w, "has_anti_bot_phrase", t->has_anti_bot_phrase);
    if (t->has_required_first_word) jw_kv_str(&w, "required_first_word", t->required_first_word);
    else jw_kv_str(&w, "required_first_word", NULL);
    jw_key(&w, "detected_injections");
    jw_arr_open(&w);
    for (int i = 0; i < t->detected_injections_count; i++) jw_str(&w, t->detected_injections[i]);
    jw_arr_close(&w);
    if (t->has_math_verification) jw_kv_str(&w, "math_verification", t->math_verification);
    else jw_kv_str(&w, "math_verification", NULL);
    jw_kv_str(&w, "sanitized_text", t->sanitized_text);
    jw_obj_close(&w);
    char *out = jw_finish(&w);
    xcpy(dst, cap, out);
    free(out);
}
