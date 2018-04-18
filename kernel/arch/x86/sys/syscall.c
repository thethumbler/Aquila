#include <core/system.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/syscall.h>

void arch_syscall(struct x86_regs *r)
{
    /* FIXME: Add some out-of-bounds checking code here */
    void (*syscall)() = syscall_table[r->eax];
    syscall(r->ebx, r->ecx, r->edx);
}

void arch_syscall_return(thread_t *thread, uintptr_t val)
{
    x86_thread_t *arch = thread->arch;

    if (thread->spawned)
        arch->regs->eax = val;
    else
        arch->eax = val;
}
