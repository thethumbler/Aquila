#ifndef _CORE_ARCH_H
#define _CORE_ARCH_H

#include <core/types.h>
#include <sys/proc.h>

/* arch/ARCH/sys/proc.c */
void arch_proc_init(struct proc *proc);
void arch_proc_kill(struct proc *proc);
void arch_init_execve(struct proc *proc, int argc, char * const _argp[], int envc, char * const _envp[]);
void arch_sleep(void);

/* arch/ARCH/sys/thread.c */
int  arch_thread_create(struct thread *thread, uintptr_t stack, uintptr_t entry, uintptr_t uentry, uintptr_t arg);
void arch_kthread_create(struct thread *thread, uintptr_t entry);
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
void arch_yield(void);  /* the current thread runs again after the ready ones */

/* arch/ARCH/sys/execve.c */
void arch_sys_execve(struct proc *proc, int argc, char * const argp[], int envc, char * const envp[]);

/* arch/ARCH/sys/signal.c */
void arch_sigreturn(void);
int  arch_sigcode_map(struct proc *proc);

/* arch/ARCH/mm/mm.c */
void arch_mm_setup(void);

paddr_t arch_page_get_mapping(struct pmap *pmap, vaddr_t vaddr);

/* the kernel's address of physical memory (the direct map, or a mapping
 * made for it) */
void *arch_phys_map(uintptr_t paddr, size_t size);

/* the kernel's address of a device's registers (memory-mapped I/O, not
 * cached) */
void *arch_mmio_map(uintptr_t paddr, size_t size);

/* an IRQ for a device's MSI, to handler: the message to write (address,
 * data); -1 if it can't (the architecture's interrupt controller) */
int arch_msi_alloc(void (*handler)(), uint64_t *addr, uint32_t *data) __attribute__((weak));

void arch_disable_interrupts(void);

void arch_reboot(void);
void arch_halt(void) __attribute__((noreturn));
void arch_poweroff(void) __attribute__((noreturn));

uint64_t arch_rtime_ns(void);
uint64_t arch_rtime_us(void);
uint64_t arch_rtime_ms(void);

/* a counter that changes as fast as the cpu runs (for randomness) */
uint64_t arch_cycles(void);

int arch_time_get(struct timespec *ts);

void arch_stack_trace(void);

#endif /* ! _CORE_ARCH_H */
