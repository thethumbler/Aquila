#ifndef _ARCH_H
#define _ARCH_H

#include <core/types.h>
#include <sys/proc.h>

/* arch/ARCH/sys/proc.c */
void arch_proc_init(struct proc *proc);
void arch_proc_spawn(struct proc *init);
void arch_proc_switch(struct proc *proc) __attribute__((noreturn));
void arch_proc_kill(struct proc *proc);
void arch_init_execve(struct proc *proc, int argc, char * const _argp[], int envc, char * const _envp[]);
void arch_sleep(void);

/* arch/ARCH/sys/thread.c */
void arch_thread_create(struct thread *thread, uintptr_t stack, uintptr_t entry, uintptr_t uentry, uintptr_t arg);
void arch_thread_kill(struct thread *thread);
void arch_thread_spawn(struct thread *thread);
void arch_thread_switch(struct thread *thread); // __attribute__((noreturn));
void arch_idle(void);

/* arch/ARCH/sys/fork.c */
int arch_proc_fork(struct thread *thread, struct proc *fork);

/* arch/ARCH/sys/syscall.c */
void arch_syscall_return(struct thread *thread, uintptr_t val);

/* arch/ARCH/sys/sched.c */
void arch_sched_init(void);
void arch_sched();
void arch_cur_thread_kill(void);// __attribute__((noreturn));

/* arch/ARCH/sys/execve.c */
void arch_sys_execve(struct proc *proc, int argc, char * const argp[], int envc, char * const envp[]);

/* arch/ARCH/sys/signal.c */
void arch_handle_signal(int sig);

/* arch/ARCH/mm/mm.c */
void arch_mm_setup(void);
//void arch_switch_mapping();
//int  arch_page_map(paddr_t paddr, vaddr_t vaddr, int flags);
//int  arch_page_unmap(vaddr_t vaddr);

paddr_t arch_page_get_mapping(vaddr_t vaddr);

void arch_disable_interrupts(void);

void arch_reboot(void);

int arch_time_get(struct timespec *ts);
//int arch_localtime(struct tm *tm);

#endif /* ! _ARCH_H */
