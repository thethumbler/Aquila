/** \file
 * The kernel lock.
 *
 * The kernel was written for one CPU, running with interrupts off: what it
 * does isn't interrupted, and nothing else runs kernel code meanwhile.
 * With several CPUs, this lock keeps that true. A CPU takes it when it
 * enters the kernel (an interrupt, an exception or a system call) from
 * user mode or from its idle loop, and lets it go when it goes back to
 * either. So user mode runs on every CPU at once, but the kernel on only
 * one at a time.
 *
 * It is a ticket lock: the CPUs that wait for it get it in the order they
 * came. While a CPU waits, it does what the CPU that holds the lock asks
 * of the others (`arch_lock_spin()`: a TLB to flush), since that CPU may
 * be waiting for them to do it. Once a CPU has the lock, it catches up on
 * what was left for then (`arch_lock_taken()`: the kernel's mappings that
 * changed).
 */

#include <core/system.h>
#include <sys/cpu.h>
#include <sys/lock.h>

static volatile uint32_t next_ticket, serving;

/* the CPU that holds the lock, or -1 */
static volatile int owner = -1;

/**
 * Takes the kernel lock, waiting for its turn if another CPU holds it.
 */
void kernel_lock(void)
{
    uint32_t ticket = __atomic_fetch_add(&next_ticket, 1, __ATOMIC_RELAXED);

    while (__atomic_load_n(&serving, __ATOMIC_ACQUIRE) != ticket)
        arch_lock_spin();

    owner = curcpu()->id;
    arch_lock_taken();
}

/**
 * Lets go of the kernel lock, for the next CPU waiting for it.
 */
void kernel_unlock(void)
{
    owner = -1;
    __atomic_store_n(&serving, serving + 1, __ATOMIC_RELEASE);
}

/**
 * Says whether this CPU holds the kernel lock.
 *
 * @return 1 if it does, 0 if not
 */
int kernel_lock_held(void)
{
    return owner == curcpu()->id;
}

/**
 * Takes the kernel lock on entering the kernel, unless this CPU holds it
 * already (an exception inside the kernel).
 *
 * @return 1 if it took the lock, for `kernel_leave()`, or 0
 */
int kernel_enter(void)
{
    if (kernel_lock_held())
        return 0;

    kernel_lock();
    return 1;
}

/**
 * Lets go of the kernel lock on leaving the kernel, if `kernel_enter()`
 * took it.
 *
 * @param took  what `kernel_enter()` returned
 */
void kernel_leave(int took)
{
    if (took)
        kernel_unlock();
}
