#include <core/system.h>
#include <core/time.h>
#include <core/arch.h>

/* XXX use a better name */
int gettime(struct timespec *ts)
{
    return arch_time_get(ts);
}

int gettimeofday(struct timeval *tv, struct timezone *tz)
{
    int err = 0;

    struct timespec ts;

    if ((err = gettime(&ts)))
        return err;

    if (tz) {
        tz->tz_minuteswest = 0;
        tz->tz_dsttime = 0;
    }

    if (tv) {
        tv->tv_sec  = ts.tv_sec;
        tv->tv_usec = ts.tv_nsec / 1000;
    }

    return 0;
}
