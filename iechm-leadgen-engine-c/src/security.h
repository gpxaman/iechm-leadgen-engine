/* The Sanitizer -- mirrors core/security.py. Separates legitimate anti-bot
 * constraints from prompt-injection attempts; injections are stripped and
 * logged, never passed downstream to the Writer. */
#ifndef IECHM_SECURITY_H
#define IECHM_SECURITY_H

#include "models.h"

TrapAnalysis sanitize(const char *raw_text);

#endif
