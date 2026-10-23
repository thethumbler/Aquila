#include <core/arch.h>
#include <core/platform.h>
#include <core/time.h>
#include <mm/mm.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/lock.h>

#include "sys.h"
#include <platform/misc.h>

static uint32_t timer_period = 0;
static uint64_t timer_ticks = 0;

uint64_t arch_cycles(void)
{
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t) hi << 32) | lo;
}

/** the tick, every CPU's: the boot CPU keeps the time */
static void x86_sched_handler(struct x86_regs *r)
{
    if (curcpu()->id) {
        sched_cpu_tick(timer_period, (r->cs & 3) == 3);
        x86_irq_tick_switch();
        return;
    }

    /* the wall clock against the real time clock, every 2^16 ticks
     * (about 2 minutes) */
    if (timer_ticks && !(timer_ticks & 0xFFFF))
        time_resync();

    ++timer_ticks;
    x86_clock_tick();
    sched_timer_tick(timer_period, (r->cs & 3) == 3);

    /* the running thread goes back to the queue (all its registers
     * saved), once the controller is done with the interrupt (irq.c) */
    x86_irq_tick_switch();
}

void arch_sched_init(void)
{
    /* the clock: the TSC or the HPET, else the ticks; then the ticks (the
     * local APIC's timer is calibrated against the clock) */
    x86_clock_init();
    timer_period = platform_timer_setup(2000000, x86_sched_handler);
    x86_clock_ticks(timer_period);

    /* the other CPUs' ticks: they start them when woken */
    for (int i = 1; i < cpus_count; ++i)
        arch_cpu_kick(&cpus[i]);
}

/** the idle loop, on the CPU's idle stack: the kernel lock goes (an
 * interrupt takes it, and lets it go if it comes back here) */
static void __arch_idle(void)
{
    if (kernel_lock_held())
        kernel_unlock();

    for (;;) {
        asm volatile("sti; hlt; cli;");
    }
}

void arch_idle(void)
{
    curthread = NULL;

    uintptr_t esp = VMA(0x100000);
    x86_kernel_stack_set(esp);
    uintptr_t stack = curcpu()->arch.idle_stack;
    extern void x86_goto(uintptr_t eip, uintptr_t ebp, uintptr_t esp) __attribute__((noreturn));
    x86_goto((uintptr_t) __arch_idle, stack, stack);
}

static void __arch_cur_thread_kill(void)
{
    /* a thread that called thread_exit is already marked as zombie, it
     * stays on its owner's thread list (for thread_join) and is freed
     * when the process exits; otherwise the process is exiting and the
     * thread was already removed from the list by proc_kill */
    int keep = curthread->state == ZOMBIE;

    thread_kill(curthread);    /* Will set the stack to VMA(0x100000) */

    if (!keep)
        kfree(curthread);

    curthread = NULL;
    kernel_idle();
}

void arch_cur_thread_kill(void)
{
    uintptr_t stack = curcpu()->arch.idle_stack;

    extern void x86_goto(uintptr_t eip, uintptr_t ebp, uintptr_t esp) __attribute__((noreturn));
    x86_goto((uintptr_t) __arch_cur_thread_kill, stack, stack);
}

void arch_yield(void)
{
    extern void x86_preempt(void);
    x86_preempt();
}

void arch_sleep(void)
{
    extern void x86_sleep(void);
    x86_sleep();
}
