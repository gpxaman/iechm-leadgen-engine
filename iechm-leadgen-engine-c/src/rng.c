#include "rng.h"
#include <time.h>
#include <unistd.h>

uint64_t rng_hash_str(const char *s) {
    uint64_t h = 1469598103934665603ULL; /* FNV offset basis */
    while (s && *s) {
        h ^= (unsigned char) *s++;
        h *= 1099511628211ULL; /* FNV prime */
    }
    return h;
}

void rng_init(Rng *r, uint64_t seed) {
    /* avoid the all-zero fixed point */
    r->state = seed ? seed : 0x9E3779B97F4A7C15ULL;
}

void rng_init_str(Rng *r, const char *seed_str) {
    rng_init(r, rng_hash_str(seed_str));
}

uint64_t rng_next_u64(Rng *r) {
    /* splitmix64 */
    uint64_t z = (r->state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

double rng_next_double(Rng *r) {
    /* top 53 bits -> [0,1) */
    uint64_t v = rng_next_u64(r) >> 11;
    return (double) v / (double) (1ULL << 53);
}

double rng_uniform(Rng *r, double lo, double hi) {
    return lo + (hi - lo) * rng_next_double(r);
}

int rng_randint(Rng *r, int lo, int hi) {
    if (hi <= lo) return lo;
    uint64_t span = (uint64_t)(hi - lo) + 1;
    return lo + (int) (rng_next_u64(r) % span);
}

size_t rng_choice_index(Rng *r, size_t n) {
    if (n == 0) return 0;
    return (size_t) (rng_next_u64(r) % n);
}

static Rng g_rng;

void global_rng_seed(void) {
    uint64_t seed = (uint64_t) time(NULL) ^ ((uint64_t) getpid() << 32);
    rng_init(&g_rng, seed);
}

Rng *global_rng(void) { return &g_rng; }
