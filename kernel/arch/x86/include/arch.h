#ifndef _X86_ARCH_H
#define _X86_ARCH_H

#include <cpu/cpu.h>

#define X86_SS		(0x20 | 3)
#define X86_EFLAGS	(0x200)
#define X86_CS		(0x18 | 3)

typedef struct
{
	uintptr_t	pd;

	uintptr_t	kstack;	/* Kernel stack */
	uintptr_t	eip;
	uintptr_t	esp;
	uintptr_t	ebp;
	uintptr_t	eflags;
	uintptr_t	eax;	/* For syscall return if process is not spawned */
	regs_t		*regs;	/* Pointer to registers on the stack */
} __attribute__((packed)) x86_proc_t;

void arch_syscall(regs_t *r);

#endif /* ! _X86_ARCH_H */
