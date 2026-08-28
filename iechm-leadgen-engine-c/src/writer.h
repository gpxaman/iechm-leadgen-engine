/* The Writer -- mirrors core/writer.py. Turns a Strategist blueprint +
 * sanitized lead into proposal text, honoring any mandatory anti-bot
 * constraint, and injects a seeded simulated model fault so the Reviewer/
 * Sentinel have something real to catch. */
#ifndef IECHM_WRITER_H
#define IECHM_WRITER_H

#include "models.h"
#include "strategist.h"

extern const double WRITER_DEFAULT_FAULT_RATE; /* 0.06 */

void draft_proposal(const QualifiedLead *lead, const StrategyChoice *strategy,
                     int attempt, double fault_rate, char *out, size_t outcap);

#endif
