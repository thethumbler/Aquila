#include <core/system.h>
#include <core/time.h>
#include <core/arch.h>
#include <core/printk.h>

/* the real time clock (CMOS: seconds) once, at the monotonic time
 * clock_base_ns; from then on the monotonic clock counts (time_resync
 * sets it again if they drift apart) */
static time_t clock_base_sec;
static uint64_t clock_base_ns;
static int clock_set;

/** the wall clock (CLOCK_REALTIME) */
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

/**
 * The real time clock again (now and then): the monotonic clock, which
 * the wall clock follows, may count a little fast or slow; 2 s or more
 * from the RTC (which counts seconds), the wall clock is the RTC's again
 */
void time_resync(void)
{
    struct timespec rtc, now;

    if (!clock_set || arch_time_get(&rtc) || gettime(&now))
        return;

    int64_t delta = (int64_t) now.tv_sec - (int64_t) rtc.tv_sec;

    if (delta >= 2 || delta <= -2) {
        printk("time: %c%d s from the real time clock, set to it\n",
               delta < 0 ? '-' : '+', (int) (delta < 0 ? -delta : delta));
        clock_base_sec = rtc.tv_sec;
        clock_base_ns  = arch_rtime_ns();
    }
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
