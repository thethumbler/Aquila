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

    /* the handler's arguments in registers (signal number, siginfo, NULL
     * ucontext); its siginfo and return address on the stack, below the
     * red zone (128 bytes the code may use under its stack pointer), 16
     * bytes aligned before the return address is pushed */
    uintptr_t info_sp = (sig_sp - 128 - sizeof(struct aquila_siginfo)) & ~(uintptr_t) 15;
    uintptr_t new_sp  = info_sp - sizeof(uintptr_t);

    if (sig_sp < 256 || !signal_frame_ok(new_sp, sig_sp - new_sp)) {
        /* can't deliver, the user stack is unusable */
        arch->kstack += sizeof(struct x86_regs);
        signal_terminate(SIGSEGV);
        return;
    }

    x86_kernel_stack_set(arch->kstack);

    int is_fault = sig == SIGSEGV || sig == SIGBUS || sig == SIGILL || sig == SIGFPE;

    *(struct aquila_siginfo *) info_sp = (struct aquila_siginfo) {
        .si_signo = sig,
        .si_addr  = is_fault ? curthread->fault_addr : 0,
    };

    /* the return address, where the page fault is a sigreturn */
    *(uintptr_t *) new_sp = 0x0FFF;

    struct x86_regs frame = {
        .rdi = sig,
        .rsi = info_sp,
        .rdx = 0,
        .rip = handler,
        .cs  = X86_CS,
        .rflags = X86_EFLAGS,
        .rsp = new_sp,
        .ss  = X86_SS,
    };

    extern void x86_jump_user_regs(struct x86_regs *) __attribute__((noreturn));
    x86_jump_user_regs(&frame);
#endif

#if ARCH_BITS==32
    /* the siginfo, then the handler's arguments (signal number, siginfo,
     * NULL ucontext) and return address, kept 4-byte aligned */
    size_t info_size  = (sizeof(struct aquila_siginfo) + 3) & ~3;
    size_t frame_size = info_size + sizeof(int) + 2 * sizeof(uintptr_t) + sizeof(uintptr_t);

    if (sig_sp < frame_size || !signal_frame_ok(sig_sp - frame_size, frame_size)) {
        /* can't deliver, the user stack is unusable */
        arch->kstack += sizeof(struct x86_regs);
        signal_terminate(SIGSEGV);
        return;
    }

    x86_kernel_stack_set(arch->kstack);

    /* siginfo */
    sig_sp -= info_size;
    struct aquila_siginfo *info = (struct aquila_siginfo *) sig_sp;
    int fault = sig == SIGSEGV || sig == SIGBUS || sig == SIGILL || sig == SIGFPE;

    *info = (struct aquila_siginfo) {
        .si_signo = sig,
        .si_addr  = fault ? curthread->fault_addr : 0,
    };

    /* Push the arguments: ucontext (none), siginfo, signal number */
    sig_sp -= sizeof(uintptr_t);
    *(uintptr_t *) sig_sp = 0;
    sig_sp -= sizeof(uintptr_t);
    *(uintptr_t *) sig_sp = (uintptr_t) info;
    sig_sp -= sizeof(int);
    *(int *) sig_sp = sig;

    /* Push return address */
    sig_sp -= sizeof(uintptr_t);
    *(uintptr_t *) sig_sp = 0x0FFF;

    extern void x86_jump_user(uintptr_t eax, uintptr_t eip, uintptr_t cs, uintptr_t eflags, uintptr_t esp, uintptr_t ss) __attribute__((noreturn));
    x86_jump_user(0, handler, X86_CS, X86_EFLAGS, sig_sp, X86_SS);
#endif
}
