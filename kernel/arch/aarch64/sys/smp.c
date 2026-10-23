/** \file
 * aarch64: one CPU (milestone 1). The kernel lock's hooks and the wake
 * IPI have nothing to do: no other CPU waits, none is idle meanwhile.
 */

#include <core/system.h>
#include <sys/cpu.h>
#include <sys/lock.h>

void arch_cpu_kick(struct cpu *cpu)
{
}

void arch_lock_spin(void)
{
}

void arch_lock_taken(void)
{
}
