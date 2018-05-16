#ifndef _ARCH_H
#define _ARCH_H

/* Include arch specific arch header */
#include <core/_arch.h>

#include <sys/proc.h>

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
void arch_thread_create(thread_t *thread, uintptr_t stack, uintptr_t entry, uintptr_t uentry, uintptr_t arg);
void arch_thread_kill(thread_t *thread);
void arch_thread_spawn(thread_t *thread);
void arch_thread_switch(thread_t *thread) __attribute__((noreturn));
void arch_idle();

/* arch/ARCH/sys/fork.c */
int arch_proc_fork(thread_t *thread, proc_t *fork);

/* arch/ARCH/sys/syscall.c */
void arch_syscall_return(thread_t *thread, uintptr_t val);

/* arch/ARCH/sys/sched.c */
void arch_sched_init();
void arch_sched();
void arch_cur_thread_kill(void) __attribute__((noreturn));

/* arch/ARCH/sys/execve.c */
void arch_sys_execve(proc_t *proc, int argc, char * const argp[], int envc, char * const envp[]);

/* arch/ARCH/sys/signal.c */
void arch_handle_signal(int sig);

/* arch/ARCH/mm/mm.c */
void arch_mm_setup();
//void arch_switch_mapping();
int  arch_page_map(paddr_t paddr, vaddr_t vaddr, int flags);
int  arch_page_unmap(vaddr_t vaddr);
paddr_t arch_page_get_mapping(vaddr_t vaddr);
void arch_mm_page_fault(vaddr_t vaddr);

#endif /* ! _ARCH_H */
