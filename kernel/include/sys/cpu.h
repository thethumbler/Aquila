#ifndef _SYS_CPU_H
#define _SYS_CPU_H

#include <core/system.h>
#include <cpu/cpu.h>    /* struct arch_cpu, curcpu() */

struct thread;

/*
 * A processor, and what runs on it: each one finds its own with curcpu()
 */
struct cpu {
    struct cpu *self;       /* curcpu() reads it: it comes first */
    int id;                 /* its index in cpus[] */
    struct thread *thread;  /* the running thread (curthread), or NULL */
    int idle;               /* none of the threads runs (kernel_idle) */
    struct arch_cpu arch;
};

#define CPUS_MAX    32

extern struct cpu cpus[CPUS_MAX];
extern int cpus_count;

#endif /* ! _SYS_CPU_H */
