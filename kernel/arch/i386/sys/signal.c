#include <core/system.h>
#include <core/arch.h>
#include <sys/signal.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/uaccess.h>
#include <mm/vm.h>
#include <cpu/cpu.h>

/* check that the signal frame can be written to the user stack */
static int signal_frame_ok(uintptr_t sp, size_t size)
{
    if (uaccess_check((void *) sp, size))
        return 0;

    /* both ends must be in a writable user mapping, otherwise the page
     * fault taken while building the frame would re-enter the signal code */
    struct vm_space *vm_space = &curproc->vm_space;
    struct vm_entry *first = vm_space_find(vm_space, sp);
    struct vm_entry *last  = vm_space_find(vm_space, sp + size - 1);

    return first && last && (first->flags & VM_UW) && (last->flags & VM_UW);
}

static void signal_terminate(int sig)
{
    curproc->exit = PROC_EXIT(sig, sig);
    proc_kill(curproc);
    arch_sleep();
}

void arch_handle_signal(int sig)
{
    /* delivering a signal abandons the system call being executed */
    curthread->in_syscall = 0;

    if (sig < 1 || sig > SIG_MAX)
        return;

    /* can't signal a zmobie */
    if (!curproc->running)
        return;

    uintptr_t handler = curproc->sigaction[sig].sa_handler;

    if (handler == SIG_IGN)
        return;

    if (handler == SIG_DFL) {
        switch (sig_default_action[sig]) {
            case SIGACT_ABORT:
            case SIGACT_TERMINATE:
                signal_terminate(sig);
                return;  /* We should never reach this anyway */
            case SIGACT_STOP:
                /* sleeps until continued (called from the thread itself,
                 * arch_thread_switch() leaves stops for later) */
                signal_stop(sig);
                return;
            default:
                /* SIGACT_IGNORE, and SIGACT_CONTINUE: continuing is done
                 * when SIGCONT is sent */
                return;
        }
    }

    struct x86_thread *arch = curthread->arch;

    /* The user context saved on entry to the kernel (by any interrupt,
     * not only syscalls) is always at the top of the kernel stack. Keep it
     * there so it is restored on signal return, and make further kernel
     * entries use the stack below it */
    arch->kstack -= sizeof(struct x86_regs);
    struct x86_regs *regs = (struct x86_regs *) arch->kstack;

#if ARCH_BITS==32
    uintptr_t sig_sp = regs->esp;
#else
    uintptr_t sig_sp = regs->rsp;
#endif

    /* signal number and return address */
    size_t frame_size = sizeof(int) + sizeof(uintptr_t);

    if (sig_sp < frame_size || !signal_frame_ok(sig_sp - frame_size, frame_size)) {
        /* can't deliver, the user stack is unusable */
        arch->kstack += sizeof(struct x86_regs);
        signal_terminate(SIGSEGV);
        return;
    }

    x86_kernel_stack_set(arch->kstack);

    /* Push signal number */
    sig_sp -= sizeof(int);
    *(int *) sig_sp = sig;

    /* Push return address */
    sig_sp -= sizeof(uintptr_t);
    *(uintptr_t *) sig_sp = 0x0FFF;

    extern void x86_jump_user(uintptr_t eax, uintptr_t eip, uintptr_t cs, uintptr_t eflags, uintptr_t esp, uintptr_t ss) __attribute__((noreturn));
#if ARCH_BITS==32
    x86_jump_user(0, handler, X86_CS, X86_EFLAGS, sig_sp, X86_SS);
#else
    x86_jump_user(0, handler, X86_CS, X86_EFLAGS, sig_sp, X86_SS);
#endif
}
