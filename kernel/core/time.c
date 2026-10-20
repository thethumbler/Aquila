#include <core/system.h>
#include <core/time.h>
#include <core/arch.h>

/* the real time clock (CMOS: seconds) once, at the monotonic time
 * clock_base_ns; from then on the monotonic clock counts */
static time_t clock_base_sec;
static uint64_t clock_base_ns;
static int clock_set;

/* XXX use a better name */
int gettime(struct timespec *ts)
{
    if (!clock_set) {
        int err = arch_time_get(ts);

        if (err)
            return err;

        clock_base_sec = ts->tv_sec;
        clock_base_ns  = arch_rtime_ns();
        clock_set = 1;
    }

    uint64_t ns = arch_rtime_ns() - clock_base_ns;

    ts->tv_sec  = clock_base_sec + ns / 1000000000ULL;
    ts->tv_nsec = ns % 1000000000ULL;
    return 0;
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
