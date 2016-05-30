#ifndef _ARCH_H
#define _ARCH_H

#include <sys/proc.h>

#if ARCH==x86
#include <arch/x86/include/arch.h>
#endif

/* arch/ARCH/sys/elf.c */
void *arch_load_elf();
void arch_load_elf_end(void *arch);

/* arch/ARCH/sys/proc.c */
void arch_init_proc(void *arch, proc_t *proc);
void arch_spawn_proc(proc_t *init);
void arch_switch_proc(proc_t *proc) __attribute__((noreturn));

/* arch/ARCH/sys/fork.c */
void arch_sys_fork(proc_t *proc);

/* arch/ARCH/sys/syscall.c */
void arch_syscall_return(proc_t *proc, uintptr_t val);

/* arch/ARCH/sys/sched.c */
void arch_sched_init();
void arch_sched();

/* arch/ARCH/sys/execve.c */
void arch_sys_execve(proc_t *proc);

void arch_idle();

#endif /* ! _ARCH_H */