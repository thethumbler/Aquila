#ifndef _QSORT_H
#define _QSORT_H

#include <core/system.h>
#include <core/string.h>

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

static inline void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *))
{
	if (nmemb <= 1 || size == 0) return;

	void *pivot = (char *) base + (nmemb - 1) * size;
	void *cur = base;
	void *wall = base;

	for (size_t i = 0; i < nmemb - 1; ++i) {
		if (compar(cur, pivot)) {
			__qsort_swap(cur, wall, size);
			wall = (char *) wall + size;
		}
		cur = (char *) cur + size;
	}

	__qsort_swap(wall, pivot, size);

	size_t idx = ((char *) wall - (char *) base) / size;

	qsort(base, idx, size, compar);
	qsort((char *) wall + size, nmemb - idx, size, compar);
}

#endif /* ! _QSORT_H */
