#ifndef _AARCH64_CPU_H
#define _AARCH64_CPU_H

#include <core/system.h>

/**
 * What an exception saves on the kernel stack (cpu/vectors.S): the
 * context it interrupted. `orig_x0` is a system call's first argument,
 * because the call's result replaces it in x[0], and a restart needs it
 * back.
 */
struct aarch64_regs {
    uint64_t x[31];     /* x0 to x30 (x30 is the link register) */
    uint64_t sp;        /* SP_EL0: user mode's stack pointer */
    uint64_t pc;        /* ELR_EL1: where it goes on from */
    uint64_t pstate;    /* SPSR_EL1 */
    uint64_t orig_x0;
    uint64_t esr;       /* ESR_EL1: why a synchronous exception came */
};

/* SPSR_EL1 and PSTATE's fields: the exception level and stack it returns
 * to, and the interrupt masks (D, A, I, F). */
#define PSTATE_EL0t     0x0
#define PSTATE_EL1h     0x5
#define PSTATE_MODE     0xF
#define PSTATE_DAIF     (0xF << 6)

/* Whether the saved context was user mode's. */
#define REGS_USER(r)    (((r)->pstate & PSTATE_MODE) == PSTATE_EL0t)

/* Reads, or writes, a system register. */

#define read_sysreg(r) ({                                   \
    uint64_t __v;                                           \
    asm volatile("mrs %0, " #r : "=r"(__v));                \
    __v;                                                    \
})

#define write_sysreg(r, v) do {                             \
    uint64_t __v = (uint64_t) (v);                          \
    asm volatile("msr " #r ", %0" :: "r"(__v));             \
} while (0)

/* Barriers: an instruction barrier, and a full data barrier. */
#define isb()   asm volatile("isb" ::: "memory")
#define dsb()   asm volatile("dsb sy" ::: "memory")

struct pmap;

/** A CPU's aarch64 state (struct cpu's arch). */
struct arch_cpu {
    struct pmap *pmap;          /* the address space in TTBR0_EL1 (pmap_switch()) */
    int tick_switch;            /* the tick asked for a thread switch */
    struct thread *fp_thread;   /* the thread whose FP/SIMD registers it has */
    uintptr_t idle_stack;       /* the top of the idle loop's stack */
};

struct cpu;

/** Returns the running CPU's struct cpu, which TPIDR_EL1, the register
 * meant for it, holds. */
static inline struct cpu *curcpu(void)
{
    return (struct cpu *) read_sysreg(tpidr_el1);
}

#include_next <cpu/cpu.h>

#endif /* ! _AARCH64_CPU_H */
