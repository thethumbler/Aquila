#ifndef _SYS_LOCK_H
#define _SYS_LOCK_H

/*
 * The kernel lock (sys/lock.c): one CPU at a time runs the kernel
 */
void kernel_lock(void);
void kernel_unlock(void);
int  kernel_lock_held(void);   /* by the running CPU */

/* an interrupt or exception: the lock taken unless the CPU has it (it
 * came from the kernel), 1 if it was; let go of on the way out if so */
int  kernel_enter(void);
void kernel_leave(int took);

/* arch: what a CPU does while it waits for the lock (requests the one
 * that has it makes, which can't wait for it to be taken), and once it
 * has it */
void arch_lock_spin(void);
void arch_lock_taken(void);

#endif /* ! _SYS_LOCK_H */
