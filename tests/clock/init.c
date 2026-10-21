/*
 * The clock: what CLOCK_MONOTONIC reads, whatever counts it (the TSC, the
 * HPET or the PIT's ticks, clock= and QEMU's options say which, see
 * tests/run.sh)
 *
 * - it never goes back, over many reads
 * - its resolution: the smallest step between two reads ("resolution_ns")
 * - nanosleep() of 100 ms takes it
 * - two marks 2 s apart (nanosleep): the host timestamps the lines
 *   (tests/clock/stamp.py) and compares
 */

#include "../lib/aqtest.h"

#define READS   200000

static uint64_t now_ns(void)
{
    struct aquila_timespec ts;
    SC(SYS_clock_gettime, CLOCK_MONOTONIC, &ts, 0);
    return (uint64_t) ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static void put64(uint64_t v)
{
    char b[24];
    int i = 23;

    b[i] = 0;
    do { b[--i] = '0' + v % 10; v /= 10; } while (v);
    aq_puts(&b[i]);
}

static void mark(const char *name)
{
    aq_puts(name);
    aq_puts(" ns=");
    put64(now_ns());
    aq_puts("\n");
}

void aq_main(void)
{
    aq_begin("clock");

    uint64_t prev = now_ns(), step = (uint64_t) -1;
    int back = 0;

    for (int i = 0; i < READS; ++i) {
        uint64_t t = now_ns();

        if (t < prev)
            back++;
        else if (t > prev && t - prev < step)
            step = t - prev;

        prev = t;
    }

    check("monotonic: never back", back, 0);
    aq_puts("resolution_ns=");
    put64(step);
    aq_puts("\n");

    struct aquila_timespec d = {0, 100000000};
    uint64_t t0 = now_ns();
    SC(SYS_nanosleep, &d, 0, 0);
    uint64_t slept = now_ns() - t0;
    check("nanosleep 100 ms: at least", slept >= 100000000ULL, 1);
    check("nanosleep 100 ms: at most 110", slept <= 110000000ULL, 1);

    mark("mark0");
    d = (struct aquila_timespec) {2, 0};
    SC(SYS_nanosleep, &d, 0, 0);
    mark("mark1");

    aq_end();
}
