/** \file
 * aarch64: the timer tick, the idle loop, and the running thread giving
 * up its CPU.
 *
 * The running thread gives up its CPU when it sleeps, yields, or is
 * killed. A thread that is killed is freed from the CPU's idle stack,
 * since its own stack goes with it.
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

/** Runs the timer tick: charges the running process for it, and asks for
 * a switch, which happens once the GIC is done with the interrupt
 * (cpu/trap.c). */
static void sched_tick(void)
{
    int user = aarch64_irq_regs && REGS_USER(aarch64_irq_regs);

    /* set the wall clock from the real time clock again, every 2^16
     * ticks */
    if (timer_ticks && !(timer_ticks & 0xFFFF))
        time_resync();

    ++timer_ticks;
    sched_timer_tick(timer_period, user);
    curcpu()->arch.tick_switch = 1;
}

/* Starts the timer tick, every 2 ms. */
void arch_sched_init(void)
{
    timer_period = timer_setup(2000000, sched_tick);
}

/** Runs the idle loop, on the CPU's idle stack. It lets go of the kernel
 * lock first; an interrupt takes it, and lets go of it again if it comes
 * back here. */
static void __attribute__((noreturn)) __arch_idle(void)
{
    if (kernel_lock_held())
        kernel_unlock();

    for (;;)
        asm volatile("msr daifclr, #2; wfi; msr daifset, #2" ::: "memory");
}

/* Idles the CPU until an interrupt makes a thread ready. It doesn't
 * return. */
void arch_idle(void)
{
    curthread = NULL;
    aarch64_goto((uintptr_t) __arch_idle, curcpu()->arch.idle_stack);
}

/** Frees the running thread, from the CPU's idle stack, and idles. */
static void __arch_cur_thread_kill(void)
{
    /* a thread that called `thread_exit()` is a zombie already, and stays
     * on its owner's thread list for `thread_join()`; otherwise its
     * process is exiting, and `proc_kill()` took it off the list */
    int keep = curthread->state == ZOMBIE;

    thread_kill(curthread);

    if (!keep)
        kfree(curthread);

    curthread = NULL;
    kernel_idle();
}

/* Ends the running thread. It is freed from the CPU's idle stack, since
 * its own stack is freed with it. It doesn't return. */
void arch_cur_thread_kill(void)
{
    aarch64_goto((uintptr_t) __arch_cur_thread_kill, curcpu()->arch.idle_stack);
}

/* Puts the running thread at the end of the ready list, and runs the
 * next one. */
void arch_yield(void)
{
    aarch64_preempt();
}

/* Puts the running thread to sleep, and runs the next ready one. It
 * returns once the thread is woken up and chosen again. */
void arch_sleep(void)
{
    struct aarch64_thread *arch = curthread->arch;

    if (!aarch64_ctx_save(&arch->ctx))
        kernel_idle();
}
