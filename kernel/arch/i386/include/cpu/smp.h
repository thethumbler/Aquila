#ifndef _X86_SMP_H
#define _X86_SMP_H

/*
 * The trampoline's pages, below 1 MiB (an AP starts in real mode): its
 * code (cpu/trampoline.S), then its page tables (cpu/smp.c)
 */
#define X86_TRAMPOLINE          0x8000
#define X86_TRAMPOLINE_SIZE     0x4000

#endif /* ! _X86_SMP_H */
