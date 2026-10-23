/** \file
 * aarch64: the FP/SIMD registers, switched lazily (as x86's FPU, see
 * processes.md): user mode's use traps (CPACR_EL1.FPEN 0b01: EL0's
 * accesses do, EL1's don't, the kernel uses none) unless the CPU's
 * registers are the running thread's already. At the trap, the thread
 * whose they are (the CPU's fp_thread) has them saved, the running one's
 * are loaded (zeros, the first time), and its use isn't trapped anymore.
 *
 * References:
 *      [1] - Arm Architecture Reference Manual for A-profile, D19.2
 *            (CPACR_EL1.FPEN) and the exception class 0x07
 */

#include <core/system.h>
#include <core/arch.h>
#include <core/string.h>
#include <cpu/cpu.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/signal.h>

MALLOC_DEFINE(M_FPSIMD, "fpsimd", "a thread's FP/SIMD registers");

#define CPACR_FPEN_EL0_TRAP     (1UL << 20)     /* 0b01 */
#define CPACR_FPEN_NONE         (3UL << 20)     /* 0b11 */

void aarch64_fpsimd_save(void *state);
void aarch64_fpsimd_load(const void *state);

#define fp_thread (curcpu()->arch.fp_thread)

static void fpen(uint64_t v)
{
    write_sysreg(cpacr_el1, v);
    isb();
}

/** going to run `thread': its use of the registers traps unless they're
 * its own already */
void aarch64_fpsimd_switch(struct thread *thread)
{
    fpen(thread && thread == fp_thread ? CPACR_FPEN_NONE : CPACR_FPEN_EL0_TRAP);
}

/** the running thread's state as it is now (from the registers if
 * they're its): saved in arch->fpsimd, NULL if it has none */
void *aarch64_fpsimd_current(struct thread *thread)
{
    struct aarch64_thread *arch = thread->arch;

    if (arch->fpsimd && fp_thread == thread)
        aarch64_fpsimd_save(arch->fpsimd);

    return arch->fpsimd;
}

/** user mode used them: the registers made the running thread's; 0, or
 * -ENOMEM (no room for its state) */
int aarch64_fpsimd_trap(void)
{
    struct thread *thread = curthread;
    struct aarch64_thread *arch = thread->arch;

    if (!arch->fpsimd && !(arch->fpsimd = kmalloc(FPSIMD_STATE_SIZE, &M_FPSIMD, M_ZERO)))
        return -ENOMEM;

    if (fp_thread != thread) {
        if (fp_thread && fp_thread->arch)
            aarch64_fpsimd_save(((struct aarch64_thread *) fp_thread->arch)->fpsimd);

        aarch64_fpsimd_load(arch->fpsimd);
        fp_thread = thread;
    }

    fpen(CPACR_FPEN_NONE);
    return 0;
}

/** the thread goes (or execs): its state, and the registers', with it */
void aarch64_fpsimd_drop(struct thread *thread)
{
    struct aarch64_thread *arch = thread->arch;

    if (fp_thread == thread) {
        fp_thread = NULL;
        fpen(CPACR_FPEN_EL0_TRAP);
    }

    if (arch) {
        kfree(arch->fpsimd);
        arch->fpsimd = NULL;
    }
}

/** fork's child: a copy of its parent's state, if it has one; 0, -ENOMEM */
int aarch64_fpsimd_fork(struct thread *parent, struct aarch64_thread *child)
{
    void *state = aarch64_fpsimd_current(parent);

    if (!state)
        return 0;

    if (!(child->fpsimd = kmalloc(FPSIMD_STATE_SIZE, &M_FPSIMD, 0)))
        return -ENOMEM;

    memcpy(child->fpsimd, state, FPSIMD_STATE_SIZE);
    return 0;
}

/** sigreturn: the state the frame kept (NULL: it had none) is the
 * thread's again, loaded at its next use */
void aarch64_fpsimd_restore(struct thread *thread, const void *state)
{
    struct aarch64_thread *arch = thread->arch;

    if (fp_thread == thread) {
        fp_thread = NULL;
        fpen(CPACR_FPEN_EL0_TRAP);
    }

    if (state && arch->fpsimd)
        memcpy(arch->fpsimd, state, FPSIMD_STATE_SIZE);
    else if (arch->fpsimd)
        memset(arch->fpsimd, 0, FPSIMD_STATE_SIZE);
}
