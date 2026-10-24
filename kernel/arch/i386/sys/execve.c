/** \file
 * x86: where a new program starts, for `execve()`. The kernel has built
 * its initial stack already (`sys/execve.c`); the thread only needs its
 * instruction and stack pointers set.
 */

#include <core/system.h>
#include <core/arch.h>
#include <sys/proc.h>

/* x86: sets the thread's saved instruction pointer, stack pointer and
 * flags, which it starts from when it next returns to user mode. The C
 * library's start code finds `argc` at the stack pointer. */
void arch_exec_start(struct thread *thread, uintptr_t entry, uintptr_t sp)
{
    struct x86_thread *arch = thread->arch;

#if ARCH_BITS==32
    arch->eip = entry;
    arch->esp = sp;
    arch->eflags = X86_EFLAGS;
#else
    arch->rip = entry;
    arch->rsp = sp;
    arch->rflags = X86_EFLAGS;
#endif
}
