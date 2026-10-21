#include <core/arch.h>
#include <core/platform.h>
#include <core/time.h>
#include <mm/mm.h>
#include <sys/proc.h>
#include <sys/sched.h>

#include "sys.h"

static uint32_t timer_period = 0;
static uint64_t timer_ticks = 0;

uint64_t arch_cycles(void)
{
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t) hi << 32) | lo;
}

static uint64_t first_measured_time = 0;

static void x86_sched_handler(struct x86_regs *r) 
{
    /* we check time every 2^16 ticks */
    if (!(timer_ticks & 0xFFFF)) {
        if (!timer_ticks) {
            struct timespec ts = {0};
            arch_time_get(&ts);     /* the real time clock itself */
            first_measured_time = ts.tv_sec;
        } else {
            struct timespec ts = {0};
            arch_time_get(&ts);

            uint64_t measured_time = ts.tv_sec - first_measured_time;
            uint64_t calculated_time = arch_rtime_ms() / 1000;

            int32_t delta = calculated_time - measured_time;

            if (ABS(delta) > 1) {
                printk("warning: calculated time differs from measured time by %c%d seconds\n", delta < 0? '-' : '+', ABS(delta));
                printk("calculated time: %ld seconds\n", calculated_time);
                printk("measured time: %ld seconds\n", measured_time);

                /* TODO: attempt to correct time */
            }
        }
    }

    ++timer_ticks;
    x86_clock_tick();
    sched_timer_tick(timer_period, (r->cs & 3) == 3);

    if (kidle) {
        schedule();
    } else {
        /* the running thread goes back to the queue, all its registers
         * saved */
        extern void x86_preempt(void);
        x86_preempt();
    }
}

void arch_sched_init(void)
{
    timer_period = platform_timer_setup(2000000, x86_sched_handler);

    /* the clock: the TSC or the HPET, else these ticks */
    x86_clock_init(timer_period);
}

static void __arch_idle(void)
{
    for (;;) {
        asm volatile("sti; hlt; cli;");
    }
}

static char __idle_stack[8192] __aligned(16);

void arch_idle(void)
{
    curthread = NULL;

    uintptr_t esp = VMA(0x100000);
    x86_kernel_stack_set(esp);
    uintptr_t stack = (uintptr_t) __idle_stack + 8192;
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
    uintptr_t stack = (uintptr_t) __idle_stack + 8192;

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
