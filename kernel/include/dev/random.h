#ifndef _DEV_RANDOM_H
#define _DEV_RANDOM_H

#include <core/system.h>

/* fill `buf' with random bytes (getrandom, /dev/random and /dev/urandom) */
void random_get(void *buf, size_t len);

/* stir something unpredictable into the pool (the cycle counter on each
 * timer tick, what is written to /dev/random) */
void random_add(uint32_t value);

#endif /* ! _DEV_RANDOM_H */
