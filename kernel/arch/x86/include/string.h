#ifndef _X86_STRING_H
#define _X86_STRING_H

#include <core/system.h>

static inline void *memcpy(void *dst, const void *src, size_t n)
{
	asm volatile (
		"cld;"
		"rep movsd;"
		"mov %3, %%ecx;"
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
		"mov %2, %%ecx;"
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
		"mov %1, %%ecx;"
		"repe cmpsb;"
		"jnz 2f;"
		"1:"
			/* Before you start blaming me for this awful solution, I tried
			 * setting direction flag and scanning the string backwards but
			 * for some reason it does not work, if you know why please elaborate !
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
		:"=g"(ret)
		:"g"(n & 3), "S"(s1),"D"(s2), "c"(n >> 2));

	return ret;
}

#endif /* !_X86_STRING_H */
