/* Deterministic, seedable PRNG standing in for Python's `random.Random(seed)`.
 * Bit-for-bit parity with CPython's Mersenne Twister is neither possible nor
 * useful here -- every call site that seeds a Python RNG (channel signal
 * generators, the writer's fault roll, per-agent fault bias) only needs its
 * OWN run to be reproducible given the same seed string, not to match the
 * Python build's actual sequence. splitmix64 gives that: same seed string ->
 * same hash -> same output sequence, every time.
 */
#ifndef IECHM_RNG_H
#define IECHM_RNG_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint64_t state;
} Rng;

/* FNV-1a over an arbitrary string, used to turn seed strings like
 * "3:freelance:Upwork" or a lead_id into a 64-bit RNG seed. */
uint64_t rng_hash_str(const char *s);

void rng_init(Rng *r, uint64_t seed);
void rng_init_str(Rng *r, const char *seed_str);

uint64_t rng_next_u64(Rng *r);
double   rng_next_double(Rng *r);           /* [0.0, 1.0) */
double   rng_uniform(Rng *r, double lo, double hi);
int      rng_randint(Rng *r, int lo, int hi); /* inclusive both ends */
size_t   rng_choice_index(Rng *r, size_t n); /* [0, n) */

/* Process-wide RNG for call sites that use Python's module-level,
 * OS-entropy-seeded `random` (e.g. strategist explore/exploit rolls). Seeded
 * once at startup; not reproducible across runs, matching the Python. */
void global_rng_seed(void);
Rng *global_rng(void);

#endif
