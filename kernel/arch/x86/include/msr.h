#ifndef _X86_MSR_H
#define _X86_MSR_H

#include <core/system.h>

static inline uint64_t x86_msr_read(uint32_t msr)
{
	uint32_t lo, hi;
	asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
	return ((uint64_t)hi << 32) | lo;
}
 
static inline void x86_msr_write(uint32_t msr, uint64_t val)
{
	uint32_t lo = val & 0xFFFFFFFF;
	uint32_t hi = val >> 32;
	asm volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}

#define X86_APIC_BASE	0x1B

#endif /* !_X86_MSR_H */