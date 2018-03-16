#ifndef _STRING_H
#define _STRING_H

#include <core/system.h>
#include <mm/mm.h>

#if ARCH==X86
#include <arch/x86/include/string.h>
#else
#endif

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

static inline char *strcpy(char *dst, char *src)
{
	char *retval = dst;
	
	while (*src)
		*dst++ = *src++;
	*dst = *src;	/* NULL terminator */

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
		if (tokens[i] == '/') {
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

#endif /* !_STRING_H */
