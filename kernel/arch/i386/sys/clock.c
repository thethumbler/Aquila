/*
 * The clock (arch_rtime_ns): nanoseconds since it started, from a
 * counter chosen once (x86_clock_init), the first of
 *
 *   - the TSC, if the CPU says it is invariant (it counts at the same
 *     rate whatever the CPU does), calibrated against the HPET (or the
 *     PIT without one) over CALIBRATE_US
 *   - the HPET's main counter, its rate known
 *   - the timer's ticks (2 ms, the local APIC's or the PIT's): what there
 *     was before
 *
 * clock=tsc|hpet|pit on the kernel's command line says which.
 *
 * A count becomes nanoseconds as (count * mult) >> 32; the timer's tick
 * moves the base on (x86_clock_tick), counts since it stay few: the
 * HPET's low 32 bits are enough (they wrap in 43 s at 100 MHz), one read
 * of its slow registers. Reads never go back. The kernel runs on one CPU, with interrupts off: they
 * don't race.
 */

#include <core/system.h>
#include <core/arch.h>
#include <core/kargs.h>
#include <core/string.h>
#include <platform/misc.h>

#define CALIBRATE_US    50000       /* 50 ms */

enum { SOURCE_PIT, SOURCE_HPET, SOURCE_TSC };

static const char *names[] = {"pit", "hpet", "tsc"};

static int source = SOURCE_PIT;
static uint64_t mult;               /* ns a count, << 32 */
static uint64_t base_count, base_ns, last_ns;

/* the PIT's ticks: their period, how many */
static uint32_t tick_period;
static uint64_t ticks;

static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (uint64_t) hi << 32 | lo;
}

static inline void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    asm volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0));
}

/* the TSC counts at a constant rate (CPUID 0x80000007, EDX bit 8) */
static int tsc_invariant(void)
{
    uint32_t a, b, c, d;

    cpuid(0x80000000, &a, &b, &c, &d);

    if (a < 0x80000007)
        return 0;

    cpuid(0x80000007, &a, &b, &c, &d);
    return !!(d & (1 << 8));
}

/* (a * b) >> 32 without its 128 bits: in 32-bit halves */
static inline uint64_t mul_shift32(uint64_t a, uint64_t b)
{
    uint64_t ah = a >> 32, al = a & 0xffffffff;
    uint64_t bh = b >> 32, bl = b & 0xffffffff;

    return ((ah * bh) << 32) + ah * bl + al * bh + ((al * bl) >> 32);
}

static uint64_t counter(void)
{
    return source == SOURCE_TSC ? rdtsc() : x86_hpet_read32();
}

/* counts since the base */
static uint64_t since(uint64_t c)
{
    return source == SOURCE_TSC ? c - base_count : (uint32_t) (c - base_count);
}

uint64_t arch_rtime_ns(void)
{
    if (source == SOURCE_PIT)
        return ticks * tick_period;

    uint64_t ns = base_ns + mul_shift32(since(counter()), mult);

    if (ns < last_ns)
        ns = last_ns;

    last_ns = ns;
    return ns;
}

uint64_t arch_rtime_us(void)
{
    return arch_rtime_ns() / 1000ULL;
}

uint64_t arch_rtime_ms(void)
{
    return arch_rtime_ns() / 1000000ULL;
}

/* the timer's tick: the base moves on */
void x86_clock_tick(void)
{
    ticks++;

    if (source == SOURCE_PIT)
        return;

    uint64_t c = counter();

    base_ns += mul_shift32(since(c), mult);
    base_count = c;
}

/* the TSC's mult: its counts over CALIBRATE_US, by the HPET or the PIT */
static uint64_t tsc_calibrate(int hpet, const char **by)
{
    uint64_t t0, t1, ns;

    if (hpet) {
        uint64_t period = x86_hpet_period_fs();
        uint64_t h0 = x86_hpet_read(), h1;
        uint64_t want = CALIBRATE_US * 1000000000ULL / period;

        t0 = rdtsc();
        while ((h1 = x86_hpet_read()) - h0 < want)
            ;
        t1 = rdtsc();

        ns = (h1 - h0) * period / 1000000ULL;
        *by = "hpet";
    } else {
        t0 = rdtsc();
        x86_pit_delay(CALIBRATE_US);
        t1 = rdtsc();

        ns = CALIBRATE_US * 1000ULL;
        *by = "pit";
    }

    return t1 > t0 ? (ns << 32) / (t1 - t0) : 0;
}

/* the timer ticks every `period_ns' from now on */
void x86_clock_ticks(uint32_t period_ns)
{
    tick_period = period_ns;

    if (source == SOURCE_PIT)
        printk("clock: timer ticks, %d ns\n", period_ns);
}

int x86_clock_precise(void)
{
    return source != SOURCE_PIT;
}

/* the clock's source (before the timer ticks) */
void x86_clock_init(void)
{
    const char *want = NULL;
    int hpet = x86_hpet_init && !x86_hpet_init();

    kargs_get("clock", &want);

    int forced = want ? !strcmp(want, "tsc") ? SOURCE_TSC : !strcmp(want, "hpet") ? SOURCE_HPET :
                 !strcmp(want, "pit") ? SOURCE_PIT : -1 : -1;

    if (want && forced < 0)
        printk("clock: clock=%s: not tsc, hpet or pit\n", want);

    int choice = forced >= 0 ? forced : tsc_invariant() ? SOURCE_TSC : hpet ? SOURCE_HPET : SOURCE_PIT;

    if (choice == SOURCE_HPET && !hpet) {
        printk("clock: no HPET\n");
        choice = tsc_invariant() ? SOURCE_TSC : SOURCE_PIT;
    }

    if (choice == SOURCE_TSC && forced != SOURCE_TSC && !tsc_invariant())
        choice = SOURCE_PIT;

    if (choice == SOURCE_TSC) {
        const char *by;

        if (forced == SOURCE_TSC && !tsc_invariant())
            printk("clock: the TSC isn't invariant (its rate may change)\n");

        mult = tsc_calibrate(hpet, &by);

        if (mult) {
            uint64_t mhz = (1000ULL << 32) / mult;
            printk("clock: tsc, %ld MHz (calibrated against the %s)\n", mhz, by);
        } else {
            choice = hpet ? SOURCE_HPET : SOURCE_PIT;
        }
    }

    if (choice == SOURCE_HPET) {
        mult = (x86_hpet_period_fs() << 32) / 1000000ULL;
        printk("clock: hpet, %ld fs a count\n", x86_hpet_period_fs());
    }

    source = choice;

    if (source != SOURCE_PIT) {
        base_count = counter();
        base_ns = last_ns = 0;
    }
}

const char *x86_clock_source(void)
{
    return names[source];
}
