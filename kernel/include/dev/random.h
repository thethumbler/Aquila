#ifndef _DEV_RANDOM_H
#define _DEV_RANDOM_H

#include <core/system.h>

/* The kernel's random numbers (dev/mem/random.c). */

/* Fills `buf` with random bytes (getrandom(), /dev/random and
 * /dev/urandom). */
void random_get(void *buf, size_t len);

/* Stirs something unpredictable into the pool: the cycle counter on each
 * timer tick, or what is written to /dev/random. */
void random_add(uint32_t value);

#endif /* ! _DEV_RANDOM_H */
