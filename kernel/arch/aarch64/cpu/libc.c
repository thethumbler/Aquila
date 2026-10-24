/** \file
 * The functions that the compiler calls on its own, to copy a structure or
 * zero a big array: memcpy, memset, memmove and memcmp. The kernel's own
 * are static inline (core/string.h), so these have no header of their
 * own.
 */

#include <stddef.h>

/** Copies `n` bytes; `dst` and `src` must not overlap. */
void *memcpy(void *dst, const void *src, size_t n)
{
    char *d = dst;
    const char *s = src;

    while (n--)
        *d++ = *s++;

    return dst;
}

/** Sets `n` bytes to `c`. */
void *memset(void *dst, int c, size_t n)
{
    char *d = dst;

    while (n--)
        *d++ = (char) c;

    return dst;
}

/** Copies `n` bytes; `dst` and `src` may overlap. */
void *memmove(void *dst, const void *src, size_t n)
{
    char *d = dst;
    const char *s = src;

    if (d < s) {
        while (n--)
            *d++ = *s++;
    } else {
        while (n--)
            d[n] = s[n];
    }

    return dst;
}

/** Compares `n` bytes. */
int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;

    for (; n--; ++x, ++y)
        if (*x != *y)
            return *x - *y;

    return 0;
}
