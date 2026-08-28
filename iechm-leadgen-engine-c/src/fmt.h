/* Shared numeric formatting -- mirrors Python's f"{x:,.2f}", used by both
 * the writer (to render a quote) and the reviewer (to verify the quoted
 * price actually appears in the draft), so it lives in one place. */
#ifndef IECHM_FMT_H
#define IECHM_FMT_H

#include "common.h"

void format_money_commas(double v, char *out, size_t cap);       /* f"{x:,.2f}" */
void format_number_commas(double v, int decimals, char *out, size_t cap); /* f"{x:,.{decimals}f}" */

/* Python's round(x, 2) -- used throughout the channel simulators for
 * budget/price figures. */
double round2(double v);

#endif
