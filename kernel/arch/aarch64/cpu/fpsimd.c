/** \file
 * aarch64: the FP/SIMD registers, switched lazily, as x86's FPU is (see
 * docs/kernel/processes.md).
 *
 * User mode's use of the registers traps (CPACR_EL1.FPEN = 0b01: EL0's
 * accesses trap, EL1's don't, and the kernel uses none), unless the CPU's
 * registers already hold the running thread's values. At the trap, the
 * thread whose values they hold (the CPU's `fp_thread`) gets them saved,
 * the running thread's are loaded (zeros, the first time), and its use
 * stops trapping.
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

/* CPACR_EL1.FPEN: user mode's use traps (0b01), or nothing does (0b11). */
#define CPACR_FPEN_EL0_TRAP     (1UL << 20)     /* 0b01 */
#define CPACR_FPEN_NONE         (3UL << 20)     /* 0b11 */

/* Saves the registers to `state`, or loads them from it (fpsimd_regs.S). */

void aarch64_fpsimd_save(void *state);
void aarch64_fpsimd_load(const void *state);

/* The thread whose values this CPU's registers hold. */
#define fp_thread (curcpu()->arch.fp_thread)

/** Sets CPACR_EL1.FPEN: whether user mode's use traps. */
static void fpen(uint64_t v)
{
    write_sysreg(cpacr_el1, v);
    isb();
}

/**
 * Gets the registers ready for a thread that is about to run: its use of
 * them traps, unless the registers already hold its values.
 *
 * @param thread  the thread about to run, or NULL
 */
void aarch64_fpsimd_switch(struct thread *thread)
{
    fpen(thread && thread == fp_thread ? CPACR_FPEN_NONE : CPACR_FPEN_EL0_TRAP);
}

/**
 * Returns a thread's FP/SIMD state as it is now, saving it from the
 * registers first if they hold it.
 *
 * @param thread  the thread
 * @return its saved state, or NULL if it never used the registers
 */
void *aarch64_fpsimd_current(struct thread *thread)
{
    struct aarch64_thread *arch = thread->arch;

    if (arch->fpsimd && fp_thread == thread)
        aarch64_fpsimd_save(arch->fpsimd);

    return arch->fpsimd;
}

/**
 * Handles the trap of user mode's use of the registers: they get the
 * running thread's values, and its use stops trapping.
 *
 * @return 0, or `-ENOMEM` if there was no memory for the thread's state
 */
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

/**
 * Throws away a thread's FP/SIMD state, when it goes or execs a new
 * program, and the registers' values if they're the thread's.
 *
 * @param thread  the thread
 */
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

/**
 * Gives a child of `fork()` a copy of its parent's FP/SIMD state, if the
 * parent has one.
 *
 * @param parent  the thread that forked
 * @param child   the child's aarch64 state
 * @return 0, or `-ENOMEM` if there was no memory for the copy
 */
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

/**
 * Gives a thread back the FP/SIMD state that a signal frame kept, for
 * `sigreturn()`. The registers are loaded with it at their next use.
 *
 * @param thread  the thread
 * @param state   the state the frame kept, or NULL if it had none (the
 *                registers are then zeros)
 */
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
