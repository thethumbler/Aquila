#ifndef _X86_MSR_H
#define _X86_MSR_H

#include <core/system.h>

/* x86: the model-specific registers (MSRs). */

/* Reads an MSR. */
static inline uint64_t msr_read(uint32_t msr)
{
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}
 
/* Writes an MSR. */
static inline void msr_write(uint32_t msr, uint64_t val)
{
    uint32_t lo = val & 0xFFFFFFFF;
    uint32_t hi = val >> 32;
    asm volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}

#define APIC_BASE   0x1B    /* the local APIC's base address MSR */

#endif /* ! _X86_MSR_H */
