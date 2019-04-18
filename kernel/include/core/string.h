#ifndef _STRING_H
#define _STRING_H

#include <core/system.h>

int snprintf(char *s, size_t n, const char *fmt, ...);

static inline void *memmove(void *dest, const void *src, size_t n)
{
    void *_dest = dest;

    if ((uintptr_t) dest < (uintptr_t) src) {
        while (n-- != 0)
            *(char *) dest++ = *(char *) src++;
    } else if ((uintptr_t) dest > (uintptr_t) src) {
        while (n != 0) {
            --n;
            *((char *) dest + n) = *((char *) src + n);
        }
    }

    return _dest;
}

static inline void *memcpy(void *dest, const void *src, size_t n)
{
    void *_dest = dest;
    while (n-- != 0)
        *(char *) dest++ = *(char *) src++;

    return _dest;
}

static inline void *memset(void *s, int c, size_t n)
{
    void *_s = s;

    while (n-- != 0) {
        *(char *) s++ = (char) c;
    }

    return _s;
}

static inline int strcmp(const char *s1, const char *s2)
{
    if (s1 == NULL || s2 == NULL)
        return 0;   // FIXME

    while (*s1 != '\0' && *s2 != '\0' && *s1 == *s2) {s1++; s2++;}

    return (int)(*s1 - *s2);
}

static inline int strncmp(const char *s1, const char *s2, size_t n)
{
    while (n-- != 0
            && *s1 != '\0'
            && *s2 != '\0'
            && *s1 == *s2) {s1++; s2++;}

    return (int)(*s1 - *s2);
}

static inline int strlen(const char *s)
{
    const char *_s = s;
    while (*_s) _s++;
    return _s - s;
}

static inline char *strdup(const char *s)
{
    int len = strlen(s);
    char *ret = kmalloc(len + 1, &M_BUFFER, 0);
    (void) memcpy(ret, s, len + 1);
    return ret;
}

static inline char *strcpy(char *dst, const char *src)
{
    char *retval = dst;
    
    while (*src != '\0')
        *dst++ = *src++;

    *dst = *src;    /* NULL terminator */

    return retval;
}

static inline char **tokenize(const char *s, char c)
{
    if (s == NULL || *s == '\0')
        return NULL;

    while (*s == c)
        ++s;

    char *tokens = strdup(s);

    if (tokens == NULL)
        return NULL;

    int len = strlen(s);

    if (!len) {
        char **ret = kmalloc(sizeof(char *), &M_BUFFER, 0);
        *ret = NULL;
        return ret;
    }

    int i, count = 0;
    for (i = 0; i < len; ++i) {
        if (tokens[i] == c) {
            tokens[i] = 0;
            ++count;
        }
    }

    if (s[len-1] != c)
        ++count;
    
    char **ret = kmalloc(sizeof(char *) * (count + 1), &M_BUFFER, 0);

    int j = 0;
    ret[j++] = tokens;

    for (i = 0; i < strlen(s) - 1; ++i)
        if (tokens[i] == 0)
            ret[j++] = &tokens[i+1];

    ret[j] = NULL;

    return ret;
}

static inline void free_tokens(char **ptr)
{
    if (!ptr) return;
    if (*ptr)
        kfree(*ptr);
    kfree(ptr);
}

#endif /* !_STRING_H */
