/** \file
 * The wall clock: the date and time of day.
 *
 * The real time clock (the CMOS clock on the PC) only counts seconds, so
 * it is read once, and from then on the wall clock follows the kernel's
 * monotonic clock (`arch_rtime_ns()`), which counts much finer.
 * `time_resync()` sets it from the real time clock again now and then, in
 * case the two drift apart.
 */

#include <core/system.h>
#include <core/time.h>
#include <core/arch.h>
#include <core/printk.h>

/* The real time clock's seconds, read at the monotonic time
 * clock_base_ns. */
static time_t clock_base_sec;
static uint64_t clock_base_ns;
static int clock_set;

/**
 * Reads the wall clock (`CLOCK_REALTIME`). The first call reads the real
 * time clock.
 *
 * @param ts  set to the time, since the epoch
 * @return 0, or a negative errno if the real time clock can't be read
 */
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
 * Sets the wall clock from the real time clock again, if they are 2
 * seconds or more apart. The tick calls it now and then: the monotonic
 * clock, which the wall clock follows, may count a little fast or slow.
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

/**
 * Reads the wall clock, in microseconds, for `gettimeofday()`.
 *
 * @param tv  if not NULL, set to the time, since the epoch
 * @param tz  if not NULL, set to UTC (the kernel has no time zones)
 * @return 0, or a negative errno if the real time clock can't be read
 */
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
