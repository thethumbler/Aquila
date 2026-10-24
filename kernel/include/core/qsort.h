#ifndef _QSORT_H
#define _QSORT_H

#include <core/system.h>
#include <core/string.h>

/* Sorting an array, as the C library's qsort() does. */

/** Swaps two elements of `size` bytes. */
static inline void __qsort_swap(char *a, char *b, size_t size)
{
    char t;
    while (size--) {
        t = *a;
        *a = *b;
        *b = t;
        ++a; ++b;
    }
}

/** Sorts an array of `nmemb` elements of `size` bytes, in the order
 * `compar` says, with quicksort (the last element as the pivot). */
static inline void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *))
{
    if (nmemb <= 1 || size == 0) return;

    void *pivot = (char *) base + (nmemb - 1) * size;
    void *cur = base;
    void *wall = base;

    for (size_t i = 0; i < nmemb - 1; ++i) {
        if (compar(cur, pivot) < 0) {
            __qsort_swap(cur, wall, size);
            wall = (char *) wall + size;
        }
        cur = (char *) cur + size;
    }

    __qsort_swap(wall, pivot, size);

    size_t idx = ((char *) wall - (char *) base) / size;

    qsort(base, idx, size, compar);
    qsort((char *) wall + size, nmemb - idx - 1, size, compar);
}

#endif /* ! _QSORT_H */
