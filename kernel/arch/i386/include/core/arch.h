#ifndef _X86_ARCH_H
#define _X86_ARCH_H

#include <cpu/cpu.h>

/* x86's side of the kernel's arch_* interface (core/arch.h): the thread's
 * own state, and where user space ends. */

/* user mode's stack and code segments, and its starting EFLAGS
 * (interrupts enabled) */
#define X86_SS      (0x20 | 3)
#define X86_EFLAGS  (0x200)
#define X86_CS      (0x18 | 3)

/** A thread's x86 state: its kernel stack, and where it resumes. */
struct x86_thread {
    uintptr_t   kstack;         /* the kernel stack's top, where the user context is */
    uintptr_t   kstack_base;    /* where the kernel stack was allocated */

#if ARCH_BITS==32
    uintptr_t   eip;
    uintptr_t   esp;
    uintptr_t   ebp;
    uintptr_t   eflags;
    uintptr_t   eax;    /* a system call's result, for a thread that hasn't run yet */
#else
    uintptr_t   rip;
    uintptr_t   rsp;
    uintptr_t   rbp;
    uintptr_t   rflags;
    uintptr_t   rax;    /* a system call's result, for a thread that hasn't run yet */
#endif

    struct x86_regs *regs;  /* user mode's registers, on the kernel stack */
    void *fpu_context;      /* its FPU state, while the FPU doesn't hold it */

    int fpu_enabled;        /* it has used the FPU (fpu.c) */
};

MALLOC_DECLARE(M_X86_THREAD);
MALLOC_DECLARE(M_KERN_STACK);

/**
 * Runs a system call that user mode made: the trap handler calls it. It
 * returns to user mode when the call is done, delivering the signals
 * that came meanwhile. Each architecture has its own, with its own saved
 * registers (sys/syscall.c).
 *
 * @param r  the registers user mode had, saved on the kernel stack
 */
void arch_syscall(struct x86_regs *r);

#if ARCH_BITS==32
#define USER_STACK      (0xC0000000UL)
#else
/* the top of the lower half, which is user space */
#define USER_STACK      (0x0000800000000000UL)
#endif
#define USER_STACK_BASE (USER_STACK - USER_STACK_SIZE)

/* the sigcode page (sys/signal.c), below the stack */
#define SIGCODE_ADDR    (USER_STACK_BASE - 0x1000)

#if ARCH_BITS==32
#define KERN_STACK_SIZE (8192U)  /* 8 KiB */
#else
/* Frames are bigger: a loop of symbolic links is 8 lookups deep, with a
 * 4 KiB path parsed at the end. */
#define KERN_STACK_SIZE (16384U) /* 16 KiB */
#endif

static inline void arch_interrupts_enable(void)
{
    asm volatile ("sti");
}

static inline void arch_interrupts_disable(void)
{
    asm volatile ("cli");
}

/* Start running in user mode, and jump to an address with a frame pointer
 * and a stack (cpu.S). */
void x86_jump_user(uintptr_t eax, uintptr_t eip, uintptr_t cs, uintptr_t eflags, uintptr_t esp, uintptr_t ss) __attribute__((noreturn));
void x86_goto(uintptr_t eip, uintptr_t ebp, uintptr_t esp) __attribute__((noreturn));

#include_next <core/arch.h>

#endif /* ! _X86_ARCH_H */
