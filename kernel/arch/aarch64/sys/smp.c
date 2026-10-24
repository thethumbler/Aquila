/** \file
 * aarch64 runs on one CPU only, so far. The kernel lock's hooks and the
 * wake IPI have nothing to do: no other CPU waits for the lock, and none
 * is idle meanwhile.
 */

#include <core/system.h>
#include <sys/cpu.h>
#include <sys/lock.h>

/*
 * Makes another CPU enter the kernel. With one CPU, there's none.
 */
void arch_cpu_kick(struct cpu *cpu)
{
}

/* Waits a little while another CPU holds the kernel lock: never, with
 * one CPU. */
void arch_lock_spin(void)
{
}

/* Called once this CPU has taken the kernel lock: nothing to do. */
void arch_lock_taken(void)
{
}
