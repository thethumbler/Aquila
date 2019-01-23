#ifndef _X86_ARCH_H
#define _X86_ARCH_H

#include <cpu/cpu.h>

#define X86_SS      (0x20 | 3)
#define X86_EFLAGS  (0x200)
#define X86_CS      (0x18 | 3)

//struct x86_proc {
//    uintptr_t   map; /* Process paging structure */
//};

struct x86_thread {
    uintptr_t   kstack; /* Kernel stack */

#if ARCH_BITS==32
    uintptr_t   eip;
    uintptr_t   esp;
    uintptr_t   ebp;
    uintptr_t   eflags;
    uintptr_t   eax;    /* For syscall return if thread is not spawned */
#else
    uintptr_t   rip;
    uintptr_t   rsp;
    uintptr_t   rbp;
    uintptr_t   rflags;
    uintptr_t   rax;    /* For syscall return if thread is not spawned */
#endif

    struct x86_regs *regs;  /* Pointer to registers on the stack */
    void *fpu_context;

    /* Flags */
    int fpu_enabled;
    int isr;
};

void arch_syscall(struct x86_regs *r);

#define USER_STACK      (0xC0000000UL)
#define USER_STACK_BASE (USER_STACK - USER_STACK_SIZE)

#define KERN_STACK_SIZE (8192U)  /* 8 KiB */

static inline void arch_interrupts_enable(void)
{
    asm volatile ("sti");
}

static inline void arch_interrupts_disable(void)
{
    asm volatile ("cli");
}

void x86_jump_user(uintptr_t eax, uintptr_t eip, uintptr_t cs, uintptr_t eflags, uintptr_t esp, uintptr_t ss) __attribute__((noreturn));
void x86_goto(uintptr_t eip, uintptr_t ebp, uintptr_t esp) __attribute__((noreturn));

struct arch_binfmt {
    uintptr_t cur_map;
    uintptr_t new_map;
};

#include_next <core/arch.h>

#endif /* ! _X86_ARCH_H */
