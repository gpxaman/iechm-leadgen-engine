#include "fmt.h"
#include <stdio.h>
#include <math.h>

double round2(double v) { return round(v * 100.0) / 100.0; }

void format_number_commas(double v, int decimals, char *out, size_t cap) {
    char fmt[16];
    snprintf(fmt, sizeof fmt, "%%.%df", decimals);
    char buf[64];
    snprintf(buf, sizeof buf, fmt, v);
    char *dot = strchr(buf, '.');
    size_t intlen = dot ? (size_t) (dot - buf) : strlen(buf);
    bool neg = buf[0] == '-';
    size_t digit_start = neg ? 1 : 0;
    size_t ndigits = intlen - digit_start;

    char result[96];
    size_t ri = 0;
    if (neg) result[ri++] = '-';
    for (size_t i = 0; i < ndigits; i++) {
        if (i > 0 && (ndigits - i) % 3 == 0) result[ri++] = ',';
        result[ri++] = buf[digit_start + i];
    }
    if (dot) {
        size_t frac_len = strlen(dot); /* includes the '.' */
        memcpy(result + ri, dot, frac_len);
        ri += frac_len;
    }
    result[ri] = '\0';
    xcpy(out, cap, result);
}

void format_money_commas(double v, char *out, size_t cap) { format_number_commas(v, 2, out, cap); }
