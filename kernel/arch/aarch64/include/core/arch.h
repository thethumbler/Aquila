#ifndef _AARCH64_ARCH_H
#define _AARCH64_ARCH_H

#include <cpu/cpu.h>

/**
 * Where a thread goes on in the kernel when it runs again: the registers
 * a function call keeps (AAPCS64: x19 to x29, the link register) and the
 * stack. aarch64_ctx_save() keeps them and returns 0; aarch64_ctx_load()
 * returns into it again, as 1 (setjmp and longjmp).
 */
struct aarch64_ctx {
    uint64_t x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;
    uint64_t fp;        /* x29 */
    uint64_t lr;        /* x30 */
    uint64_t sp;
};

int  aarch64_ctx_save(struct aarch64_ctx *ctx) __attribute__((returns_twice));
void aarch64_ctx_load(struct aarch64_ctx *ctx) __attribute__((noreturn));

struct aarch64_thread {
    uintptr_t kstack;       /* its kernel stack's top */
    uintptr_t kstack_base;  /* where it was allocated */

    struct aarch64_ctx ctx; /* in the kernel, switched away from */

    /* its user registers, saved at the top of its kernel stack when it
     * entered the kernel */
    struct aarch64_regs *regs;

    /* user mode's, for a thread that never ran (spawned at them) */
    uint64_t pc, sp, x0, x1, x2;
};

MALLOC_DECLARE(M_AARCH64_THREAD);
MALLOC_DECLARE(M_KERN_STACK);

/* the top of the lower half, user space (48 bits; the stack's top
 * below it, as x86-64's) */
#define USER_STACK      (0x0000800000000000UL)
#define USER_STACK_BASE (USER_STACK - USER_STACK_SIZE)

/* the sigcode page (sys/signal.c), below the stack */
#define SIGCODE_ADDR    (USER_STACK_BASE - 0x1000)

#define KERN_STACK_SIZE (16384U) /* 16 KiB */

/* IRQs (DAIF.I): the kernel runs with them masked */
static inline void arch_interrupts_enable(void)
{
    asm volatile ("msr daifclr, #2" ::: "memory");
}

static inline void arch_interrupts_disable(void)
{
    asm volatile ("msr daifset, #2" ::: "memory");
}

/* user mode, at `pc' with `sp' and x0 to x2, on the kernel stack `kstack'
 * (where the next exception from user mode lands) */
void aarch64_jump_user(uintptr_t pc, uintptr_t sp, uintptr_t x0, uintptr_t x1,
                       uintptr_t x2, uintptr_t kstack) __attribute__((noreturn));

/* on the kernel stack `sp', `fn' runs (and doesn't return) */
void aarch64_goto(uintptr_t fn, uintptr_t sp) __attribute__((noreturn));

/* the way back to user mode: signals, restarts (sys/signal.c) */
void aarch64_return_to_user(struct aarch64_regs *r, int syscall);

void arch_syscall(struct aarch64_regs *r);

#include_next <core/arch.h>

#endif /* ! _AARCH64_ARCH_H */
