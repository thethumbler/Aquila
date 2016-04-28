#ifndef _X86_ARCH_H
#define _X86_ARCH_H

typedef struct 
{
	uint32_t edi, esi, ebp, ebx, ecx, edx, eax,
		eip, cs, eflags, esp, ss;
} __attribute__((packed)) x86_stat_t;

typedef struct
{
	uintptr_t	pd;
	x86_stat_t	stat;
} __attribute__((packed)) x86_proc_t;

#endif /* ! _X86_ARCH_H */