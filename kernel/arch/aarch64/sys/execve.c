/** \file
 * aarch64: where a new program starts, for `execve()`. The kernel has
 * built its initial stack already (`sys/execve.c`); the thread only needs
 * its program counter and stack pointer set.
 */

#include <core/system.h>
#include <core/arch.h>
#include <sys/proc.h>

/* aarch64: sets the thread's saved program counter and stack pointer,
 * which it starts from when it next returns to user mode, and drops its
 * FP/SIMD state: the new program's registers are zeros at their first
 * use. The C library's start code finds `argc` at the stack pointer. */
void arch_exec_start(struct thread *thread, uintptr_t entry, uintptr_t sp)
{
    struct aarch64_thread *arch = thread->arch;

    aarch64_fpsimd_drop(thread);

    arch->pc = entry;
    arch->sp = sp;
}
