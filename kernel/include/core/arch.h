#ifndef _ARCH_H
#define _ARCH_H

#include <sys/proc.h>

#if ARCH==x86
#include <arch/x86/include/arch.h>
#endif

void *arch_load_elf();
void arch_init_proc(void *arch, proc_t *proc, uintptr_t entry);
void arch_load_elf_end(void *arch);
void arch_idle();
void arch_switch_process(proc_t *proc);

#endif /* ! _ARCH_H */