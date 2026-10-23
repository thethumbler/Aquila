#ifndef _CORE_TIME_H
#define _CORE_TIME_H

#include <core/types.h>

int gettime(struct timespec *ts);

int gettimeofday(struct timeval *tv, struct timezone *tz);
int settimeofday(const struct timeval *tv, const struct timezone *tz);
void time_resync(void);

#endif /* ! _CORE_TIME_H */
