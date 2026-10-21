/*
 * Signals on x86: the way back to user mode, and the handler's frame
 *
 * Every return to user mode (a system call's, an interrupt's, an
 * exception's) goes through x86_return_to_user(): the process takes its
 * signals there (signal_take: stops, default actions), a system call a
 * signal interrupted is restarted or fails with EINTR, and a handler runs
 * by rewriting the user context saved on the kernel stack. The kernel
 * stack is never left halfway: a signal can't come anywhere else.
 *
 * The handler's frame, on the user stack: what it returns to (the
 * sigcode page), its arguments (in registers on x86-64), then
 *
 *      struct sigframe: the siginfo, the mask to restore, the
 *      interrupted registers and FPU state
 *
 * The handler returns into the sigcode page, mapped in every process
 * (SIGCODE_ADDR, shared, read and execute), which makes the sigreturn
 * system call: the frame is read back, its registers checked (user
 * segments, no privileged flags), the mask and the FPU state restored.
 */

#include <core/system.h>
#include <core/arch.h>
#include <core/string.h>
#include <cpu/cpu.h>
#include <ds/hashmap.h>
#include <mm/mm.h>
#include <mm/pmap.h>
#include <mm/vm.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/signal.h>
#include <sys/uaccess.h>

#include <aquila/syscall.h>

#if ARCH_BITS==32
#define REG_IP(r)   ((r)->eip)
#define REG_SP(r)   ((r)->esp)
#define REG_AX(r)   ((r)->eax)
#define REG_FLAGS(r) ((r)->eflags)
#else
#define REG_IP(r)   ((r)->rip)
#define REG_SP(r)   ((r)->rsp)
#define REG_AX(r)   ((r)->rax)
#define REG_FLAGS(r) ((r)->rflags)
#endif

/* the flags user code may set: the arithmetic ones, TF, DF, AC, ID (not
 * IF, IOPL, NT, VM, ...) */
#define USER_FLAGS  0x00240DD5UL

struct sigframe {
    struct aquila_siginfo info;
    sigset_t mask;              /* sigreturn restores it */
    uint32_t fpu;               /* fxsave below is the thread's state */
    struct x86_regs regs;       /* the interrupted context */
    uint8_t fxsave[512] __aligned(16);
} __aligned(16);

/* below the frame, i386: the return address and the handler's arguments
 * (signal, siginfo, ucontext), padded so the handler is entered as a call
 * would (16-byte aligned stack before the call) */
#define I386_ARGS   20

/*
 * The sigcode page: mov $SYS_sigreturn, %eax; int $0x80; ud2
 */
static const uint8_t sigcode[] = {
    0xB8, SYS_sigreturn & 0xff, (SYS_sigreturn >> 8) & 0xff, 0, 0,
    0xCD, 0x80,
    0x0F, 0x0B,
};

static int vm_page_eq(void *a, void *b)
{
    return ((struct vm_page *) a)->off == *(size_t *) b;
}

/* the page, made once: the object keeps it, every process maps it */
static struct vm_page *sigcode_in(struct vm_object *vm_object, size_t off)
{
    char *buf = kmalloc(PAGE_SIZE, &M_BUFFER, M_ZERO);
    struct vm_page *vm_page = buf ? mm_page_alloc() : NULL;

    if (!vm_page) {
        kfree(buf);
        return NULL;
    }

    memcpy(buf, sigcode, sizeof(sigcode));
    pmap_page_write(vm_page->paddr, 0, PAGE_SIZE, buf);
    kfree(buf);

    vm_page->vm_object = vm_object;
    vm_page->off = 0;
    vm_page->ref = 1;
    vm_object_page_insert(vm_object, vm_page);

    return vm_page;
}

static struct vm_pager sigcode_pager = {
    .in = sigcode_in,
};

static struct vm_object sigcode_object = {
    .pager = &sigcode_pager,
    .ref   = 1,
};

/* the sigcode page in proc's (new) address space */
int arch_sigcode_map(struct proc *proc)
{
    if (!sigcode_object.pages && !(sigcode_object.pages = hashmap_new(0, vm_page_eq)))
        return -ENOMEM;

    struct vm_entry *vm_entry = kmalloc(sizeof(struct vm_entry), &M_VM_ENTRY, M_ZERO);

    if (!vm_entry)
        return -ENOMEM;

    vm_entry->base  = SIGCODE_ADDR;
    vm_entry->size  = PAGE_SIZE;
    vm_entry->flags = VM_URX;
    vm_entry->vm_object = &sigcode_object;

    int err = vm_space_insert(&proc->vm_space, vm_entry);

    if (err) {
        kfree(vm_entry);
        return err;
    }

    vm_object_incref(&sigcode_object);
    return 0;
}

/* both ends of [sp, sp + size) in a writable user mapping: writing the
 * frame can fault only to bring pages in */
static int user_writable(uintptr_t sp, size_t size)
{
    if (uaccess_check((void *) sp, size))
        return 0;

    struct vm_space *vm_space = &curproc->vm_space;
    struct vm_entry *first = vm_space_find(vm_space, sp);
    struct vm_entry *last  = vm_space_find(vm_space, sp + size - 1);

    return first && last && (first->flags & VM_UW) && (last->flags & VM_UW);
}

/* the handler of `sig' runs when `r' is returned to */
static void deliver(struct x86_regs *r, int sig)
{
    struct thread *thread = curthread;
    struct proc *proc = curproc;
    struct aquila_sigaction *sa = &proc->sigaction[sig];
    uintptr_t handler = sa->sa_handler;

    /* the mask to come back to: sigsuspend's and pselect's is the one
     * they replaced */
    sigset_t mask = thread->sig_oldmask_set ? thread->sig_oldmask : proc->sigmask;
    thread->sig_oldmask_set = 0;

#if ARCH_BITS==32
    uintptr_t sp = REG_SP(r);
#else
    /* below the red zone (128 bytes the code may use under its stack) */
    uintptr_t sp = REG_SP(r) - 128;
#endif

    uintptr_t f = (sp - sizeof(struct sigframe)) & ~(uintptr_t) 15;

#if ARCH_BITS==32
    uintptr_t new_sp = f - I386_ARGS;
#else
    uintptr_t new_sp = f - sizeof(uintptr_t);
#endif

    /* nowhere to put it: as a fault would, it can't go on */
    if (sp < 4096 || !user_writable(new_sp, sp - new_sp))
        signal_exit(SIGSEGV);

    struct sigframe *frame = (struct sigframe *) f;
    int fault = sig == SIGSEGV || sig == SIGBUS || sig == SIGILL || sig == SIGFPE;

    frame->info = (struct aquila_siginfo) {
        .si_signo = sig,
        .si_addr  = fault ? thread->fault_addr : 0,
    };
    frame->mask = mask;
    frame->regs = *r;
    frame->fpu  = x86_fpu_save_to(frame->fxsave);

    /* the handler starts with a clean FPU */
    x86_fpu_reset();

#if ARCH_BITS==32
    uintptr_t *args = (uintptr_t *) new_sp;
    args[0] = SIGCODE_ADDR;
    args[1] = sig;
    args[2] = (uintptr_t) &frame->info;
    args[3] = 0;
    args[4] = 0;
#else
    *(uintptr_t *) new_sp = SIGCODE_ADDR;
    r->rdi = sig;
    r->rsi = (uintptr_t) &frame->info;
    r->rdx = 0;
#endif

    REG_IP(r) = handler;
    REG_SP(r) = new_sp;
    REG_FLAGS(r) = (REG_FLAGS(r) & USER_FLAGS & ~0x400UL) | X86_EFLAGS;  /* DF clear */

    /* blocked while it runs: its mask, and itself (unless SA_NODEFER) */
    sigset_t block = sa->sa_mask | (sa->sa_flags & SA_NODEFER ? 0 : 1UL << sig);
    proc->sigmask = (mask | block) & ~SIG_UNMASKABLE;

    if (sa->sa_flags & SA_RESETHAND)
        sa->sa_handler = SIG_DFL;
}

void x86_return_to_user(struct x86_regs *r, int syscall)
{
    struct thread *thread = curthread;

    if (!thread || !thread->arch || (r->cs & 3) != 3)
        return;

    struct proc *proc = thread->owner;
    int sig = signal_take();

    /* a system call a signal interrupted: again (no handler, or one that
     * says SA_RESTART), else EINTR */
    if (syscall) {
        intptr_t ret = (intptr_t) REG_AX(r);

        if (ret == -ERESTARTSYS || ret == -ERESTARTNOHAND) {
            int restart = !sig || (ret == -ERESTARTSYS && (proc->sigaction[sig].sa_flags & SA_RESTART));

            if (restart) {
                /* back to the int $0x80, the number in eax */
                REG_IP(r) -= 2;
                REG_AX(r) = thread->syscall_nr;
            } else {
                REG_AX(r) = -EINTR;
            }
        }
    }

    if (sig) {
        deliver(r, sig);
    } else if (thread->sig_oldmask_set) {
        /* sigsuspend restarted: the mask it replaced back */
        proc->sigmask = thread->sig_oldmask;
        thread->sig_oldmask_set = 0;
    }
}

/* sigreturn(): the frame of the handler that returns, back */
void arch_sigreturn(void)
{
    struct x86_thread *arch = curthread->arch;
    struct x86_regs *r = arch->regs;

#if ARCH_BITS==32
    uintptr_t f = REG_SP(r) + I386_ARGS - sizeof(uintptr_t);
#else
    uintptr_t f = REG_SP(r);
#endif

    struct sigframe frame;

    if ((f & 15) || !user_writable(f, sizeof(frame))) {
        signal_force(curproc, SIGSEGV);
        return;
    }

    memcpy(&frame, (void *) f, sizeof(frame));

    /* its registers, but user mode's segments, and no flags it couldn't
     * set itself */
    uintptr_t flags = (REG_FLAGS(&frame.regs) & USER_FLAGS) | X86_EFLAGS;

    *r = frame.regs;
    REG_FLAGS(r) = flags;
    r->cs = X86_CS;
    r->ss = X86_SS;

#if ARCH_BITS==32
    r->ds = r->es = r->fs = r->gs = X86_SS;
#endif

    curproc->sigmask = frame.mask & ~SIG_UNMASKABLE;
    x86_fpu_load_from(frame.fxsave, frame.fpu);
}
