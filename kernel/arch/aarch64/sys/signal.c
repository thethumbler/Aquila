/** \file
 * Signals on aarch64: the way back to user mode, and the handler's frame
 * (signals.md; as x86's sys/signal.c)
 *
 * Every return to user mode goes through aarch64_return_to_user(): the
 * process takes its signals there (signal_take), a system call a signal
 * interrupted is restarted (back to its svc, its first argument back in
 * x0: the result took its place) or fails with EINTR, and a handler runs
 * by rewriting the user context saved on the kernel stack.
 *
 * The handler's frame, on the user stack: struct sigframe (the siginfo,
 * the mask to restore, the interrupted registers). It is called with x0
 * the signal, x1 the siginfo, x2 no ucontext, and returns (x30) into the
 * sigcode page, which makes the sigreturn system call.
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

/* the flags user code may set: N, Z, C, V */
#define USER_FLAGS  0xF0000000UL

struct sigframe {
    struct aquila_siginfo info;
    sigset_t mask;              /* sigreturn restores it */
    struct aarch64_regs regs;   /* the interrupted context */
} __aligned(16);

/*
 * The sigcode page: mov x8, #SYS_sigreturn; svc #0; udf #0
 */
static const uint32_t sigcode[] = {
    0xD2800000 | (SYS_sigreturn << 5) | 8,
    0xD4000001,
    0x00000000,
};

static int vm_page_eq(void *a, void *b)
{
    return ((struct vm_page *) a)->off == *(size_t *) b;
}

/** the page, made once: the object keeps it, every process maps it */
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

    /* written through the direct map, run from user mode's: the
     * instruction cache made to see it */
    asm volatile("dc cvau, %0; dsb ish; ic ivau, %0; dsb ish; isb"
                 :: "r"(PHYS2VIRT(vm_page->paddr)) : "memory");

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

/** the sigcode page in proc's (new) address space */
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

/** the handler of `sig' runs when `r' is returned to */
static void deliver(struct aarch64_regs *r, int sig)
{
    struct thread *thread = curthread;
    struct proc *proc = curproc;
    struct aquila_sigaction *sa = &proc->sigaction[sig];

    /* the mask to come back to: sigsuspend's and pselect's is the one
     * they replaced */
    sigset_t mask = thread->sig_oldmask_set ? thread->sig_oldmask : proc->sigmask;
    thread->sig_oldmask_set = 0;

    uintptr_t f = (r->sp - sizeof(struct sigframe)) & ~(uintptr_t) 15;

    /* the frame, made here (zeroed: nothing of the kernel's goes out) */
    struct sigframe frame;
    struct sigframe *uframe = (struct sigframe *) f;
    int fault = sig == SIGSEGV || sig == SIGBUS || sig == SIGILL || sig == SIGFPE;

    memset(&frame, 0, sizeof(frame));
    frame.info = (struct aquila_siginfo) {
        .si_signo = sig,
        .si_addr  = fault ? thread->fault_addr : 0,
    };
    frame.mask = mask;
    frame.regs = *r;

    /* nowhere to put it: as a fault would, it can't go on */
    if (r->sp < 4096 || copyout(uframe, &frame, sizeof(frame)))
        signal_exit(SIGSEGV);

    r->x[0]  = sig;
    r->x[1]  = (uintptr_t) &uframe->info;
    r->x[2]  = 0;
    r->x[30] = SIGCODE_ADDR;
    r->pc    = sa->sa_handler;
    r->sp    = f;
    r->pstate &= USER_FLAGS;

    /* blocked while it runs: its mask, and itself (unless SA_NODEFER) */
    sigset_t block = sa->sa_mask | (sa->sa_flags & SA_NODEFER ? 0 : 1UL << sig);
    proc->sigmask = (mask | block) & ~SIG_UNMASKABLE;

    if (sa->sa_flags & SA_RESETHAND)
        sa->sa_handler = SIG_DFL;
}

void aarch64_return_to_user(struct aarch64_regs *r, int syscall)
{
    struct thread *thread = curthread;

    if (!thread || !thread->arch || !REGS_USER(r))
        return;

    struct proc *proc = thread->owner;
    int sig = signal_take();

    /* a system call a signal interrupted: again (no handler, or one that
     * says SA_RESTART), else EINTR */
    if (syscall) {
        intptr_t ret = (intptr_t) r->x[0];

        if (ret == -ERESTARTSYS || ret == -ERESTARTNOHAND) {
            int restart = !sig || (ret == -ERESTARTSYS && (proc->sigaction[sig].sa_flags & SA_RESTART));

            if (restart) {
                /* back to the svc, its first argument back (the number
                 * is still in x8) */
                r->pc -= 4;
                r->x[0] = r->orig_x0;

                /* straight back into the same call: it may go on where
                 * it was */
                thread->restarting = !sig;
            } else {
                r->x[0] = -EINTR;
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

/** sigreturn(): the frame of the handler that returns, back */
void arch_sigreturn(void)
{
    struct aarch64_thread *arch = curthread->arch;
    struct aarch64_regs *r = arch->regs;
    uintptr_t f = r->sp;

    struct sigframe frame;

    if ((f & 15) || copyin(&frame, (void *) f, sizeof(frame))) {
        signal_force(curproc, SIGSEGV);
        return;
    }

    /* its registers, but user mode's level, and only the flags it could
     * set itself */
    uint64_t pstate = frame.regs.pstate & USER_FLAGS;

    *r = frame.regs;
    r->pstate = pstate;

    curproc->sigmask = frame.mask & ~SIG_UNMASKABLE;
}
