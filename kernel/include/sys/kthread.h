#ifndef _SYS_KTHREAD_H
#define _SYS_KTHREAD_H

#include <core/system.h>
#include <sys/proc.h>

/**
 * process 0, the kernel's: its threads run in the kernel only (on
 * the kernel's address space), and it takes no signals
 */
extern struct proc *kproc;

/* a thread of process 0 that runs fn(arg), named `name' (it must not
 * return); it is ready to run */
int kthread_create(const char *name, void (*fn)(void *), void *arg, struct thread **ref);

#endif /* ! _SYS_KTHREAD_H */
