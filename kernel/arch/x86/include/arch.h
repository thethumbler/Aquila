#ifndef _X86_ARCH_H
#define _X86_ARCH_H

#include <cpu/cpu.h>

#define X86_SS		(0x20 | 3)
#define X86_EFLAGS	(0x200)
#define X86_CS		(0x18 | 3)

typedef struct 
{
	uint32_t edi, esi, ebp, ebx, ecx, edx, eax,
		eip, cs, eflags, esp, ss;
} __attribute__((packed)) x86_stat_t;

typedef struct
{
	uintptr_t	pd;
	x86_stat_t	stat;

	uintptr_t	kstack;	/* Kernel stack */
	uintptr_t	eip;
	uintptr_t	esp;
	uintptr_t	ebp;
	uintptr_t	eflags;
	regs_t		*regs;	/* Pointer to registers on the stack */
} __attribute__((packed)) x86_proc_t;

void arch_syscall(regs_t *r);

#endif /* ! _X86_ARCH_H */