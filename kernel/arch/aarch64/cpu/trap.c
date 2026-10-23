/** \file
 * aarch64: exceptions to the kernel's events (interrupts.md): a system
 * call, a page fault (mm_page_fault()), another fault (a signal, or a
 * panic in the kernel), an interrupt (its handler, the tick's switch)
 *
 * References:
 *      [1] - Arm Architecture Reference Manual for A-profile, D1.10
 *            (Exception entry) and ESR_EL1's exception classes (D19.2)
 */

#include <core/system.h>
#include <core/arch.h>
#include <core/panic.h>
#include <cpu/cpu.h>
#include <mm/mm.h>
#include <sys/lock.h>
#include <sys/sched.h>
#include <sys/signal.h>
#include <platform/virt.h>

/* ESR_EL1's exception class */
#define EC(esr)         (((esr) >> 26) & 0x3F)
#define EC_UNKNOWN      0x00    /* an undefined instruction */
#define EC_FP           0x07    /* FP/SIMD, trapped (CPACR_EL1.FPEN) */
#define EC_SVC64        0x15
#define EC_IABT_LOW     0x20    /* instruction abort, from EL0 */
#define EC_IABT_CUR     0x21
#define EC_PC_ALIGN     0x22
#define EC_DABT_LOW     0x24    /* data abort, from EL0 */
#define EC_DABT_CUR     0x25
#define EC_SP_ALIGN     0x26
#define EC_BRK          0x3C

/* an abort's ISS: write not read (data aborts), the fault's status code */
#define ISS_WNR         (1 << 6)
#define ISS_FSC(esr)    ((esr) & 0x3F)
#define FSC_PERMISSION  0x0C    /* 0b0011xx: a permission fault, a page there */

static void dump(struct aarch64_regs *r)
{
    for (int i = 0; i < 31; i += 2)
        printk("x%d = %p  x%d = %p\n", i, (void *) r->x[i], i + 1, i + 1 < 31 ? (void *) r->x[i + 1] : NULL);

    printk("sp = %p  pc = %p  pstate = %p  esr = %p  far = %p\n", (void *) r->sp,
           (void *) r->pc, (void *) r->pstate, (void *) r->esr, (void *) read_sysreg(far_el1));
}

/** a copy from or to user memory that faulted (cpu/uaccess.S): it goes on
 * at its fixup, which returns what wasn't copied */
static int uaccess_fixup(struct aarch64_regs *r)
{
    extern uintptr_t aarch64_uaccess_fixups[], aarch64_uaccess_fixups_end[];

    for (uintptr_t *f = aarch64_uaccess_fixups; f < aarch64_uaccess_fixups_end; f += 2) {
        if (r->pc == f[0]) {
            r->pc = f[1];
            return 1;
        }
    }

    return 0;
}

static void abort(struct aarch64_regs *r, int ec)
{
    uintptr_t far = read_sysreg(far_el1);
    int user = REGS_USER(r);
    int flags = user ? PF_USER : 0;

    if (ec == EC_IABT_LOW || ec == EC_IABT_CUR)
        flags |= PF_EXEC;
    else
        flags |= r->esr & ISS_WNR ? PF_WRITE : PF_READ;

    if ((ISS_FSC(r->esr) & 0x3C) == FSC_PERMISSION)
        flags |= PF_PRESENT;

    /* the process's memory, maybe not there yet (a user address only:
     * the kernel's own is all mapped) */
    if (far < USER_STACK && curthread && !mm_page_fault(far, flags)) {
        aarch64_return_to_user(r, 0);
        return;
    }

    if (user) {
        signal_force(curproc, SIGSEGV);
        aarch64_return_to_user(r, 0);
        return;
    }

    /* the kernel's, copying from or to user memory: -EFAULT */
    if (far < USER_STACK && uaccess_fixup(r))
        return;

    printk("kernel: page fault at %p%s\n", (void *) far,
           far < USER_STACK ? " (user memory, outside copyin/copyout)" : "");
    dump(r);
    panic("page fault in the kernel");
}

static void sync_dispatch(struct aarch64_regs *r)
{
    int ec = EC(r->esr);

    switch (ec) {
        case EC_SVC64:
            if (!curthread)
                panic("syscall without a current thread");

            ((struct aarch64_thread *) curthread->arch)->regs = r;
            arch_syscall(r);
            return;

        case EC_DABT_LOW:
        case EC_DABT_CUR:
        case EC_IABT_LOW:
        case EC_IABT_CUR:
            abort(r, ec);
            return;

        case EC_FP:
            /* user mode's first use since a switch: the registers
             * made its own (no room for them: it can't go on) */
            if (REGS_USER(r)) {
                if (aarch64_fpsimd_trap())
                    signal_force(curproc, SIGKILL);

                aarch64_return_to_user(r, 0);
                return;
            }
            break;
    }

    if (REGS_USER(r)) {
        /* what it can't do: an undefined instruction, a misaligned PC
         * or stack, a breakpoint */
        int sig = ec == EC_PC_ALIGN || ec == EC_SP_ALIGN ? SIGBUS :
                  ec == EC_BRK ? SIGTRAP : SIGILL;

        curthread->fault_addr = r->pc;
        signal_force(curproc, sig);
        aarch64_return_to_user(r, 0);
        return;
    }

    printk("kernel: exception, class %x\n", ec);
    dump(r);
    panic("exception in the kernel");
}

/** a synchronous exception (a system call, a fault): with the kernel
 * lock, taken if it came from user mode */
void aarch64_sync(struct aarch64_regs *r)
{
    int took = kernel_enter();

    /* its process was killed while it ran (on another CPU): it goes */
    if (took && curthread && curthread->killed)
        sched_thread_killed();

    sync_dispatch(r);
    kernel_leave(took);
}

/* the interrupted context, for the tick's handler (user or kernel) */
struct aarch64_regs *aarch64_irq_regs;

/** this CPU's thread stops here, ready, and the next one runs: when
 * it's chosen again, it goes on from here */
void aarch64_preempt(void)
{
    struct aarch64_thread *arch = curthread->arch;

    if (!aarch64_ctx_save(&arch->ctx))
        schedule();
}

/** an interrupt: with the kernel lock, taken if it came from user mode
 * or the idle loop, let go of on the way back there */
void aarch64_irq(struct aarch64_regs *r)
{
    int took = kernel_enter();

    aarch64_irq_regs = r;

    if (took && curthread && curthread->killed) {
        irq_handle();
        sched_thread_killed();
    }

    irq_handle();

    /* the tick: the running thread back to the queue, the next one runs
     * (the CPU was idle: it runs) */
    if (curcpu()->arch.tick_switch) {
        curcpu()->arch.tick_switch = 0;

        if (curcpu()->idle)
            schedule();
        else
            aarch64_preempt();
    }

    sched_idle_wakeup();

    /* an urgent thread was woken (the network's): it runs now */
    if (sched_urgent && REGS_USER(r) && curthread)
        aarch64_preempt();

    aarch64_return_to_user(r, 0);
    kernel_leave(took);
}

void aarch64_bad(struct aarch64_regs *r)
{
    printk("kernel: an exception the vectors don't take (FIQ, SError, aarch32)\n");
    dump(r);
    panic("unexpected exception");
}
