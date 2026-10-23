/** \file
 * aarch64: the tick, the idle loop, a thread that stops (sleeps, yields)
 * or goes (killed)
 */

#include <core/arch.h>
#include <core/time.h>
#include <mm/mm.h>
#include <sys/cpu.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/lock.h>
#include <platform/virt.h>

static uint64_t timer_period;
static uint64_t timer_ticks;

extern struct aarch64_regs *aarch64_irq_regs;
void aarch64_preempt(void);

/** the tick: the running process charged, a switch asked for (done once
 * the GIC is done with the interrupt: cpu/trap.c) */
static void sched_tick(void)
{
    int user = aarch64_irq_regs && REGS_USER(aarch64_irq_regs);

    /* the wall clock against the real time clock, every 2^16 ticks */
    if (timer_ticks && !(timer_ticks & 0xFFFF))
        time_resync();

    ++timer_ticks;
    sched_timer_tick(timer_period, user);
    curcpu()->arch.tick_switch = 1;
}

void arch_sched_init(void)
{
    timer_period = timer_setup(2000000, sched_tick);
}

/** the idle loop, on the CPU's idle stack: the kernel lock goes (an
 * interrupt takes it, and lets it go if it comes back here) */
static void __attribute__((noreturn)) __arch_idle(void)
{
    if (kernel_lock_held())
        kernel_unlock();

    for (;;)
        asm volatile("msr daifclr, #2; wfi; msr daifset, #2" ::: "memory");
}

void arch_idle(void)
{
    curthread = NULL;
    aarch64_goto((uintptr_t) __arch_idle, curcpu()->arch.idle_stack);
}

static void __arch_cur_thread_kill(void)
{
    /* a thread that called thread_exit is already marked as zombie, it
     * stays on its owner's thread list (for thread_join); otherwise the
     * process is exiting and the thread was already removed from the
     * list by proc_kill */
    int keep = curthread->state == ZOMBIE;

    thread_kill(curthread);

    if (!keep)
        kfree(curthread);

    curthread = NULL;
    kernel_idle();
}

/** the running thread goes, from the CPU's idle stack (its own is freed) */
void arch_cur_thread_kill(void)
{
    aarch64_goto((uintptr_t) __arch_cur_thread_kill, curcpu()->arch.idle_stack);
}

/** the running thread to the end of the ready queue, the next one runs */
void arch_yield(void)
{
    aarch64_preempt();
}

/** the running thread stops here (it sleeps), the next ready one runs:
 * when it's woken and chosen, it goes on from here */
void arch_sleep(void)
{
    struct aarch64_thread *arch = curthread->arch;

    if (!aarch64_ctx_save(&arch->ctx))
        kernel_idle();
}
