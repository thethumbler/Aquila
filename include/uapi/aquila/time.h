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
    unsigned long tms_utime;    /* user time */
    unsigned long tms_stime;    /* system time */
    unsigned long tms_cutime;   /* user time of waited for children */
    unsigned long tms_cstime;   /* system time of waited for children */
};

/* setitimer(2): the real time timer (SIGALRM) of a process, in ns; 0 is
 * none (value), or only once (interval) */
#define AQUILA_ITIMER_REAL  0

struct aquila_itimer {
    uint64_t value;     /* until it expires */
    uint64_t interval;  /* then again every interval */
};

#endif /* ! _AQUILA_TIME_H */
