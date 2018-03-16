#ifndef _ARCH_H
#define _ARCH_H

#include <sys/proc.h>

#if ARCH==x86
#include <arch/x86/include/arch.h>
#endif

/* arch/ARCH/sys/binfmt.c */
void *arch_binfmt_load();
void arch_binfmt_end(void *arch);

/* arch/ARCH/sys/proc.c */
void arch_proc_init(void *arch, proc_t *proc);
void arch_proc_spawn(proc_t *init);
void arch_proc_switch(proc_t *proc) __attribute__((noreturn));
void arch_proc_kill(proc_t *proc);
void arch_sleep();

/* arch/ARCH/sys/thread.c */
void arch_thread_create(thread_t *thread, uintptr_t stack, uintptr_t entry, uintptr_t arg);
void arch_thread_kill(thread_t *thread);
void arch_thread_spawn(thread_t *thread);
void arch_thread_switch(thread_t *thread) __attribute__((noreturn));

/* arch/ARCH/sys/fork.c */
int arch_proc_fork(thread_t *thread, proc_t *fork);

/* arch/ARCH/sys/syscall.c */
void arch_syscall_return(thread_t *thread, uintptr_t val);

/* arch/ARCH/sys/sched.c */
void arch_sched_init();
void arch_sched();

/* arch/ARCH/sys/execve.c */
void arch_sys_execve(proc_t *proc, int argc, char * const argp[], int envc, char * const envp[]);

void arch_idle();

#endif /* ! _ARCH_H */
