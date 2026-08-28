/* Layer 1 -- structured extraction. Mirrors core/classify.py: deterministic
 * keyword/heuristic extraction standing in for the "LLM + Pydantic" parser
 * from the source architecture. */
#ifndef IECHM_CLASSIFY_H
#define IECHM_CLASSIFY_H

#include "models.h"

ManufacturingDomain classify_domain(const char *text);
ClientArchetype classify_archetype(const RawSignal *signal);

/* Appends matched terms into out[LIST_MAX][LIST_ITEM_LEN], sets *out_count. */
void extract_cad_software(const char *text, char out[LIST_MAX][LIST_ITEM_LEN], int *out_count);
void extract_materials(const char *text, char out[LIST_MAX][LIST_ITEM_LEN], int *out_count);
void extract_deliverables(const char *text, char out[LIST_MAX][LIST_ITEM_LEN], int *out_count);

const char *classify_project_stage(const char *text); /* static string literal */

#endif
