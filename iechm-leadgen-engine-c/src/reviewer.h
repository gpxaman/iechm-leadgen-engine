/* The Reviewer -- mirrors core/reviewer.py. Deterministic QA gate,
 * independently re-verifies everything the Writer/Strategist did. */
#ifndef IECHM_REVIEWER_H
#define IECHM_REVIEWER_H

#include "models.h"

#define MAX_REWRITES 2

typedef struct {
    bool approved;
    char notes[NOTES_MAX][NOTE_LEN];
    int notes_count;
    bool needs_rewrite;
} ReviewResult;

ReviewResult review_proposal(const QualifiedLead *lead, const char *proposal_text, int rewrite_count);

#endif
