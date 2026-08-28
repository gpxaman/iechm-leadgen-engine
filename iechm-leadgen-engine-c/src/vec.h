/* Type-erased growable array (append-only, doubling capacity) used by
 * store.c to hold each table's rows. This is the one place in the codebase
 * that relies on realloc, kept deliberately small and generic so it only
 * has to be gotten right once. */
#ifndef IECHM_VEC_H
#define IECHM_VEC_H

#include "common.h"

typedef struct {
    void *items;
    int count;
    int cap;
    size_t elem_size;
} Vec;

void vec_init(Vec *v, size_t elem_size);
void *vec_push(Vec *v);      /* returns pointer to a new, zeroed element */
void vec_clear(Vec *v);      /* count = 0, keeps allocated capacity */
void *vec_at(const Vec *v, int i);
void vec_reserve(Vec *v, int min_cap); /* grows capacity only, doesn't touch count */

#endif
