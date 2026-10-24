#ifndef _AARCH64_ARCH_H
#define _AARCH64_ARCH_H

#include <cpu/cpu.h>

/**
 * Where a thread goes on in the kernel when it runs again: the registers
 * that a function call keeps (AAPCS64: x19 to x29, and the link
 * register), and the stack pointer. aarch64_ctx_save() saves them and
 * returns 0; aarch64_ctx_load() later returns from that same call again,
 * this time with 1 (as setjmp() and longjmp() do).
 */
struct aarch64_ctx {
    uint64_t x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;
    uint64_t fp;        /* x29 */
    uint64_t lr;        /* x30 */
    uint64_t sp;
};

/* Saves the current context, or switches to a saved one (cpu/switch.S). */
int  aarch64_ctx_save(struct aarch64_ctx *ctx) __attribute__((returns_twice));
void aarch64_ctx_load(struct aarch64_ctx *ctx) __attribute__((noreturn));

/** A thread's aarch64 state (thread->arch). */
struct aarch64_thread {
    uintptr_t kstack;       /* the top of its kernel stack */
    uintptr_t kstack_base;  /* where its kernel stack was allocated */

    struct aarch64_ctx ctx; /* where it was switched away from, in the kernel */

    /* its user registers, saved at the top of its kernel stack when it
     * entered the kernel */
    struct aarch64_regs *regs;

    /* for a thread that never ran, the user mode registers it starts
     * with */
    uint64_t pc, sp, x0, x1, x2;

    /* its FP/SIMD registers, once it has used them (cpu/fpsimd.c) */
    void *fpsimd;
};

/* The size of the FP/SIMD state: v0 to v31, FPSR and FPCR. */
#define FPSIMD_STATE_SIZE   528

/* The FP/SIMD registers, switched lazily (cpu/fpsimd.c). */
struct thread;
void  aarch64_fpsimd_switch(struct thread *thread);
int   aarch64_fpsimd_trap(void);
void *aarch64_fpsimd_current(struct thread *thread);
void  aarch64_fpsimd_drop(struct thread *thread);
int   aarch64_fpsimd_fork(struct thread *parent, struct aarch64_thread *child);
void  aarch64_fpsimd_restore(struct thread *thread, const void *state);

MALLOC_DECLARE(M_AARCH64_THREAD);
MALLOC_DECLARE(M_KERN_STACK);

/* The top of the lower half of the address space, which is user space
 * (48 bits), where the user stack ends, as on x86-64. */
#define USER_STACK      (0x0000800000000000UL)
#define USER_STACK_BASE (USER_STACK - USER_STACK_SIZE)

/* The page with the code that calls sigreturn() (sys/signal.c), below
 * the stack. */
#define SIGCODE_ADDR    (USER_STACK_BASE - 0x1000)

/* The size of a thread's kernel stack. */
#define KERN_STACK_SIZE (16384U) /* 16 KiB */

/** Unmasks IRQs (DAIF.I). The kernel runs with them masked. */
static inline void arch_interrupts_enable(void)
{
    asm volatile ("msr daifclr, #2" ::: "memory");
}

/** Masks IRQs. */
static inline void arch_interrupts_disable(void)
{
    asm volatile ("msr daifset, #2" ::: "memory");
}

/* Goes to user mode at `pc`, with `sp` and x0 to x2. `kstack` becomes the
 * kernel stack that the next exception from user mode lands on. */
void aarch64_jump_user(uintptr_t pc, uintptr_t sp, uintptr_t x0, uintptr_t x1,
                       uintptr_t x2, uintptr_t kstack) __attribute__((noreturn));

/* Runs `fn`, which must not return, on the kernel stack `sp`. */
void aarch64_goto(uintptr_t fn, uintptr_t sp) __attribute__((noreturn));

/* Delivers the signals and restarts the system call, on the way back to
 * user mode (sys/signal.c). */
void aarch64_return_to_user(struct aarch64_regs *r, int syscall);

/* Runs a system call (sys/syscall.c). */
void arch_syscall(struct aarch64_regs *r);

#include_next <core/arch.h>

#endif /* ! _AARCH64_ARCH_H */
