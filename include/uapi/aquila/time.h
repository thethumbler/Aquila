#ifndef _AQUILA_TIME_H
#define _AQUILA_TIME_H

#include <stdint.h>

/* clock ids of clock_gettime(2), the C library defines these too */
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME      1
#define CLOCK_MONOTONIC     4   /* time since boot */
#endif

/* times(2), in clock ticks of 1/AQUILA_CLK_TCK seconds */
#define AQUILA_CLK_TCK      100

struct aquila_tms {
    uint32_t tms_utime;     /* user time */
    uint32_t tms_stime;     /* system time */
    uint32_t tms_cutime;    /* user time of waited for children */
    uint32_t tms_cstime;    /* system time of waited for children */
};

#endif /* ! _AQUILA_TIME_H */
