#ifndef _X86_ARCH_H
#define _X86_ARCH_H

#include <cpu/cpu.h>

#define X86_SS      (0x20 | 3)
#define X86_EFLAGS  (0x200)
#define X86_CS      (0x18 | 3)

typedef struct {
    uintptr_t   pd; /* Page Directory */
} __attribute__((packed)) x86_proc_t;

typedef struct {
    uintptr_t   kstack; /* Kernel stack */
    uintptr_t   eip;
    uintptr_t   esp;
    uintptr_t   ebp;
    uintptr_t   eflags;
    uintptr_t   eax;    /* For syscall return if thread is not spawned */

    struct x86_regs *regs;  /* Pointer to registers on the stack */

    void        *fpu_context;

    /* Flags */
    int fpu_enabled : 1;
    int isr : 1;

} __attribute__((packed)) x86_thread_t;

void arch_syscall(struct x86_regs *r);

#define USER_STACK      (0xC0000000UL)
#define USER_STACK_BASE (USER_STACK - USER_STACK_SIZE)

#define KERN_STACK_SIZE (8192U)  /* 8 KiB */

static inline void arch_interrupts_enable()
{
    asm volatile ("sti");
}

static inline void arch_interrupts_disable()
{
    asm volatile ("cli");
}

#endif /* ! _X86_ARCH_H */
