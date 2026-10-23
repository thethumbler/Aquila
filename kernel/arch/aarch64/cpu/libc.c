/** \file
 * What the compiler calls on its own (a struct's copy, a big array's
 * zeroing): memcpy, memset, memmove, memcmp. The kernel's are static
 * inline (core/string.h), so these have no header of their own.
 */

#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n)
{
    char *d = dst;
    const char *s = src;

    while (n--)
        *d++ = *s++;

    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    char *d = dst;

    while (n--)
        *d++ = (char) c;

    return dst;
}

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

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;

    for (; n--; ++x, ++y)
        if (*x != *y)
            return *x - *y;

    return 0;
}
