#ifndef _SYS_CPU_H
#define _SYS_CPU_H

#include <core/system.h>
#include <cpu/cpu.h>    /* struct arch_cpu, curcpu() */

struct thread;

/** A processor, and what runs on it. Each CPU finds its own with
 * curcpu(). */
struct cpu {
    struct cpu *self;       /* curcpu() reads it, so it comes first */
    int id;                 /* its index in cpus[] */
    struct thread *thread;  /* the running thread (curthread), or NULL */
    int idle;               /* it runs no thread (kernel_idle()) */
    int kicked;             /* it was woken up to run one (sched_cpu_wake()) */
    struct arch_cpu arch;
};

/* The most CPUs the kernel uses. */
#define CPUS_MAX    32

/* The CPUs; cpus[0] is the one that booted. */
extern struct cpu cpus[CPUS_MAX];
extern int cpus_count;

/**
 * Makes another CPU enter the kernel now, with an inter-processor
 * interrupt: to run a thread that just became ready if it's idle, or so
 * that its thread sees it was killed or takes its signals. Each
 * architecture implements it; with one CPU, it does nothing.
 *
 * @param cpu  the CPU
 */
void arch_cpu_kick(struct cpu *cpu);

#endif /* ! _SYS_CPU_H */
