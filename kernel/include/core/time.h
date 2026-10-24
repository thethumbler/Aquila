#ifndef _CORE_TIME_H
#define _CORE_TIME_H

#include <core/types.h>

/* The wall clock (core/time.c). */

/* Reads the wall clock. */
int gettime(struct timespec *ts);

/* Reads the wall clock in microseconds, for gettimeofday(). */
int gettimeofday(struct timeval *tv, struct timezone *tz);

/* Sets the wall clock from the real time clock again, if they drifted
 * apart. */
void time_resync(void);

#endif /* ! _CORE_TIME_H */
