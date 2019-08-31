#ifndef _X86_STRING_H
#define _X86_STRING_H

#include <core/system.h>
#include <mm/mm.h>

static inline void *memcpy(void *dst, const void *src, size_t n)
{
    asm volatile (
        "cld;"
        "rep movsd;"
#if ARCH_BITS==32
        "mov %3, %%ecx;"
#else
        "movq %3, %%rcx;"
#endif
        "rep movsb;"
        :"=S"((int){0}), "=D"((int){0}), "=c"((int){0})
        :"g"(n & 3), "S"(src), "D"(dst), "c"(n >> 2)
        :"memory");
    return dst;
}

static inline void *memset(void *s, int _c, size_t n)
{
    uint8_t c = _c & 0xFF;
    uint32_t f = (c << 0x00) | (c << 0x08) | (c << 0x10) | (c << 0x18);
    asm volatile (
        "cld;"
        "rep stosl;"
#if ARCH_BITS==32
        "mov %2, %%ecx;"
#else
        "movq %2, %%rcx;"
#endif
        "rep stosb;"
        :"=D"((int){0}), "=c"((int){0})
        :"g"(n & 3), "D"(s), "a"(f), "c"(n >> 2)
        :"memory");

    return s;
}


static inline int strncmp(const char *s1, const char *s2, size_t n)
{
    char ret = 0;
    asm volatile(
        "cld;"
        "repe cmpsd;"
        "jnz 1f;"
        "mov %4, %%ecx;"
        "repe cmpsb;"
        "jnz 2f;"
        "1:"
            /* Before you start blaming me for this awful solution, I tried
             * setting direction flag and scanning the string backwards but
             * for some reason it does not work, if you know why please elaborate!
             */
            "mov $4, %%ecx;"
            "sub $4, %%esi;"
            "sub $4, %%edi;"
            "repe cmpsb;"
        "2:"
            "dec %%esi;"
            "dec %%edi;"
            "mov (%%esi), %%cl;"
            "subb (%%edi), %%cl;"
            "mov %%cl, %0;"
        :"=g"(ret), "=S"((int){0}), "=D"((int){0}), "=c"((int){0})
        :"g"(n & 3), "S"(s1),"D"(s2), "c"(n >> 2));

    return ret;
}

int snprintf(char *s, size_t n, const char *fmt, ...);

static inline int strcmp(const char *s1, const char *s2)
{
    while (*s1 && *s2 && *s1 == *s2) {s1++; s2++;}
    return *s1 - *s2;
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
    char *ret = kmalloc(len + 1);
    memcpy(ret, s, len + 1);
    return ret;
}

static inline char *strcpy(char *dst, const char *src)
{
    char *retval = dst;
    
    while (*src)
        *dst++ = *src++;
    *dst = *src;    /* NULL terminator */

    return retval;
}

static inline char **tokenize(const char *s, char c)
{
    if (!s || !*s) return NULL;

    while (*s == c)
        ++s;

    char *tokens = strdup(s);
    int len = strlen(s);

    if (!len) {
        char **ret = kmalloc(sizeof(char *));
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
    
    char **ret = kmalloc(sizeof(char *) * (count + 1));

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

#endif /* !_X86_STRING_H */
