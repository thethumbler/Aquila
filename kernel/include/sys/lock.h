#ifndef _SYS_LOCK_H
#define _SYS_LOCK_H

/*
 * The kernel lock (sys/lock.c): only one CPU at a time runs the kernel.
 * See docs/kernel/smp.md.
 */

/* Takes and lets go of the lock; says whether this CPU holds it. */
void kernel_lock(void);
void kernel_unlock(void);
int  kernel_lock_held(void);

/* For an interrupt or an exception: kernel_enter() takes the lock unless
 * this CPU holds it already (it came from the kernel), and returns 1 if it
 * took it; kernel_leave() then lets go of it on the way out. */
int  kernel_enter(void);
void kernel_leave(int took);

/** Runs in the kernel lock's wait loop, on each turn. The architecture
 * answers the requests that the lock's holder makes of the other CPUs
 * (a TLB flush), which can't wait until the lock is free. */
void arch_lock_spin(void);

/** Runs once this CPU has taken the kernel lock. The architecture catches
 * up with what changed while it waited (the kernel's mappings). */
void arch_lock_taken(void);

#endif /* ! _SYS_LOCK_H */
