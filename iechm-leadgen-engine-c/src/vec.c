#include "vec.h"
#include <stdlib.h>
#include <stdio.h>

void vec_init(Vec *v, size_t elem_size) {
    v->items = NULL;
    v->count = 0;
    v->cap = 0;
    v->elem_size = elem_size;
}

void *vec_push(Vec *v) {
    if (v->count >= v->cap) {
        int ncap = v->cap ? v->cap * 2 : 32;
        void *n = realloc(v->items, (size_t) ncap * v->elem_size);
        if (!n) { fprintf(stderr, "vec: out of memory\n"); exit(1); }
        v->items = n;
        v->cap = ncap;
    }
    char *slot = (char *) v->items + (size_t) v->count * v->elem_size;
    memset(slot, 0, v->elem_size);
    v->count++;
    return slot;
}

void vec_clear(Vec *v) { v->count = 0; }

void vec_reserve(Vec *v, int min_cap) {
    if (min_cap <= v->cap) return;
    int ncap = v->cap ? v->cap : 32;
    while (ncap < min_cap) ncap *= 2;
    void *n = realloc(v->items, (size_t) ncap * v->elem_size);
    if (!n) { fprintf(stderr, "vec: out of memory\n"); exit(1); }
    v->items = n;
    v->cap = ncap;
}

void *vec_at(const Vec *v, int i) { return (char *) v->items + (size_t) i * v->elem_size; }
