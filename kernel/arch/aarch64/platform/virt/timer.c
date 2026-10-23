/** \file
 * The tick: the virtual timer of ARM's generic timer, a CPU's own (PPI
 * 27). Its counter (CNTVCT_EL0) runs at CNTFRQ_EL0, from boot: it's the
 * clock too (arch_rtime_ns()).
 *
 * References:
 *      [1] - Arm Architecture Reference Manual for A-profile, chapter D11
 *            (The Generic Timer in AArch64 state)
 */

#include <core/system.h>
#include <cpu/cpu.h>
#include <dev/irq.h>
#include <platform/virt.h>

static uint64_t freq, interval;
static void (*tick)(void);

static void timer_irq(void *arg __unused)
{
    /* the next one, `interval' counts from now */
    write_sysreg(cntv_tval_el0, interval);

    if (tick)
        tick();
}

/** the tick, every `period_ns', to `handler'; the period it is */
uint64_t timer_setup(uint64_t period_ns, void (*handler)(void))
{
    freq = read_sysreg(cntfrq_el0);
    interval = freq * period_ns / 1000000000ULL;
    tick = handler;

    irq_install(VIRT_TIMER_IRQ, timer_irq, NULL);

    write_sysreg(cntv_tval_el0, interval);
    write_sysreg(cntv_ctl_el0, 1);      /* enabled, not masked */
    isb();

    printk("timer: %ld Hz counter, the tick every %ld counts\n", freq, interval);
    return interval * 1000000000ULL / freq;
}

/* the clock: the counter's time since boot */
uint64_t arch_rtime_ns(void)
{
    uint64_t f = read_sysreg(cntfrq_el0), c = read_sysreg(cntvct_el0);
    return c / f * 1000000000ULL + c % f * 1000000000ULL / f;
}

uint64_t arch_rtime_us(void)
{
    return arch_rtime_ns() / 1000;
}

uint64_t arch_rtime_ms(void)
{
    return arch_rtime_ns() / 1000000;
}

uint64_t arch_cycles(void)
{
    return read_sysreg(cntvct_el0);
}
