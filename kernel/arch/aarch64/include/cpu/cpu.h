#ifndef _AARCH64_CPU_H
#define _AARCH64_CPU_H

#include <core/system.h>

/**
 * What an exception saves on the kernel stack (cpu/vectors.S): the
 * interrupted context. `orig_x0' is a system call's first argument, which
 * its result replaces in x[0] (a restart needs it back).
 */
struct aarch64_regs {
    uint64_t x[31];     /* x0 to x30 (x30 the link register) */
    uint64_t sp;        /* SP_EL0: user mode's stack */
    uint64_t pc;        /* ELR_EL1: where it goes on */
    uint64_t pstate;    /* SPSR_EL1 */
    uint64_t orig_x0;
    uint64_t esr;       /* ESR_EL1: why (a synchronous exception) */
};

/* SPSR_EL1/PSTATE: the exception level and stack it returns to, the
 * interrupt masks (D, A, I, F) */
#define PSTATE_EL0t     0x0
#define PSTATE_EL1h     0x5
#define PSTATE_MODE     0xF
#define PSTATE_DAIF     (0xF << 6)

#define REGS_USER(r)    (((r)->pstate & PSTATE_MODE) == PSTATE_EL0t)

#define read_sysreg(r) ({                                   \
    uint64_t __v;                                           \
    asm volatile("mrs %0, " #r : "=r"(__v));                \
    __v;                                                    \
})

#define write_sysreg(r, v) do {                             \
    uint64_t __v = (uint64_t) (v);                          \
    asm volatile("msr " #r ", %0" :: "r"(__v));             \
} while (0)

#define isb()   asm volatile("isb" ::: "memory")
#define dsb()   asm volatile("dsb sy" ::: "memory")

struct pmap;

/** the CPU's own (struct cpu's arch) */
struct arch_cpu {
    struct pmap *pmap;          /* TTBR0_EL1's (pmap_switch) */
    int tick_switch;            /* the tick asked for a switch */
    uintptr_t idle_stack;       /* the idle loop's, its top */
};

/* the running CPU's struct cpu: in TPIDR_EL1, the register for it */
struct cpu;

static inline struct cpu *curcpu(void)
{
    return (struct cpu *) read_sysreg(tpidr_el1);
}

#include_next <cpu/cpu.h>

#endif /* ! _AARCH64_CPU_H */
