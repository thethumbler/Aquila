/*
 * The kernel lock
 *
 * The kernel was written for one CPU, running with interrupts off: what
 * it does isn't interrupted, nothing else runs it meanwhile. With more
 * CPUs, the lock keeps that true: a CPU takes it when it enters the
 * kernel (an interrupt, an exception, a system call) from user mode or
 * from its idle loop, and lets it go when it goes back to either. User
 * mode runs on every CPU at once, the kernel on one at a time.
 *
 * A ticket lock: the CPUs that wait get it in the order they came. While
 * it waits, a CPU does what the one that has it asks of the others
 * (arch_lock_spin(): a TLB to flush), the one that has it waiting for
 * them to have done it; once it has it, what was left for then
 * (arch_lock_taken(): the kernel's mappings that changed).
 */

#include <core/system.h>
#include <sys/cpu.h>
#include <sys/lock.h>

static volatile uint32_t next_ticket, serving;

/* the CPU that has it, -1 */
static volatile int owner = -1;

void kernel_lock(void)
{
    uint32_t ticket = __atomic_fetch_add(&next_ticket, 1, __ATOMIC_RELAXED);

    while (__atomic_load_n(&serving, __ATOMIC_ACQUIRE) != ticket)
        arch_lock_spin();

    owner = curcpu()->id;
    arch_lock_taken();
}

void kernel_unlock(void)
{
    owner = -1;
    __atomic_store_n(&serving, serving + 1, __ATOMIC_RELEASE);
}

int kernel_lock_held(void)
{
    return owner == curcpu()->id;
}

int kernel_enter(void)
{
    if (kernel_lock_held())
        return 0;

    kernel_lock();
    return 1;
}

void kernel_leave(int took)
{
    if (took)
        kernel_unlock();
}
