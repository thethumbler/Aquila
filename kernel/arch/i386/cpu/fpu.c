#include <core/system.h>
#include <core/panic.h>
#include <core/assert.h>
#include <core/arch.h>
#include <core/string.h>
#include <cpu/cpu.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/signal.h>

MALLOC_DEFINE(M_X86_FPU, "x86-fpu", "x86 FPU context");

#define CPUID_EDX_FXSR  _BV(24)
#define CPUID_EDX_SSE   _BV(25)

/* fxsave/fxrstor require a 16-byte aligned area */
static char fpu_context[512] __aligned(16);
static int fpu_fxsr = 0;

/* the thread whose state the running CPU's FPU has */
#define last_fpu_thread (curcpu()->arch.fpu_thread)

/* MXCSR's bits the CPU takes (fxsave's MXCSR_MASK, 0xFFBF if it says 0),
 * a frame's MXCSR is masked with it: fxrstor would fault in the kernel */
static uint32_t mxcsr_mask = 0xFFBF;

/* the state a thread starts with (fninit's, and MXCSR's default): not the
 * last owner's XMM registers */
static char fpu_clean[512] __aligned(16);

/*
 * Lazy FPU switching is done using CR0.TS, the first FPU/SSE instruction
 * executed while TS is set raises #NM (x86_fpu_trap). CR0.EM must stay
 * clear, otherwise SSE instructions raise #UD instead.
 */
void x86_fpu_setup(void)
{
    uint32_t eax = 1, ebx, ecx, edx;
    asm volatile("cpuid":"+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));

    write_cr0((read_cr0() & ~CR0_EM) | CR0_MP | CR0_NE | CR0_TS);

    if (edx & CPUID_EDX_FXSR) {
        fpu_fxsr = 1;
        uintptr_t cr4 = read_cr4() | CR4_OSFXSR;

        if (edx & CPUID_EDX_SSE)
            cr4 |= CR4_OSXMMEXCPT;

        write_cr4(cr4);

        /* a clean state, and the mask, from the FPU as it is now */
        x86_fpu_enable();
        asm volatile("fninit");
        asm volatile("fxsave (%0)"::"r"(fpu_clean):"memory");
        x86_fpu_disable();

        memset(fpu_clean + 32, 0, sizeof(fpu_clean) - 32);  /* the registers */
        *(uint32_t *) (fpu_clean + 24) = 0x1F80;            /* MXCSR */

        uint32_t mask = *(uint32_t *) (fpu_clean + 28);
        if (mask)
            mxcsr_mask = mask;
    }
}

void x86_fpu_enable(void)
{
    asm volatile("clts");
}

void x86_fpu_disable(void)
{
    write_cr0(read_cr0() | CR0_TS);
}

void x86_fpu_init(void)
{
    assert_alignof(fpu_context, 16);

    if (fpu_fxsr) {
        memcpy(fpu_context, fpu_clean, 512);
        asm volatile("fxrstor (%0)"::"r"(fpu_context):"memory");
    } else {
        asm volatile("fninit");
    }
}

static inline void fpu_save(void)
{
    if (fpu_fxsr)
        asm volatile("fxsave (%0)"::"r"(fpu_context):"memory");
    else
        asm volatile("fnsave (%0)"::"r"(fpu_context):"memory");
}

static inline void fpu_restore(void)
{
    if (fpu_fxsr)
        asm volatile("fxrstor (%0)"::"r"(fpu_context):"memory");
    else
        asm volatile("frstor (%0)"::"r"(fpu_context):"memory");
}

void x86_fpu_trap(void)
{
    if (!curthread)
        panic("FPU trap without a current thread");

    struct x86_thread *arch = curthread->arch;

    /* Allocate the context save area of the thread on its first FPU use,
     * so saving it when switching away later can never fail */
    if (!arch->fpu_context) {
        arch->fpu_context = kmalloc(512, &M_X86_FPU, 0);

        if (!arch->fpu_context) {
            /* Can't give it the FPU, the thread stays without it */
            signal_proc_send(curproc, SIGKILL);
            return;
        }
    }

    x86_fpu_enable();

    /* We already own the FPU */
    if (curthread == last_fpu_thread)
        return;

    if (last_fpu_thread) {  /* Save context of the previous owner */
        struct x86_thread *_arch = last_fpu_thread->arch;
        fpu_save();
        memcpy(_arch->fpu_context, fpu_context, 512);
    }

    if (arch->fpu_enabled) {    /* Restore context */
        memcpy(fpu_context, arch->fpu_context, 512);
        fpu_restore();
    } else {                    /* First use */
        x86_fpu_init();
        arch->fpu_enabled = 1;
    }

    last_fpu_thread = curthread;
}

/*
 * The current thread is switched out: its state, if the FPU has it, saved
 * now (it may run on another CPU next), the FPU no one's
 */
void x86_fpu_switch_out(void)
{
    if (!curthread || last_fpu_thread != curthread)
        return;

    struct x86_thread *arch = curthread->arch;

    x86_fpu_enable();
    fpu_save();
    memcpy(arch->fpu_context, fpu_context, 512);
    last_fpu_thread = NULL;
}

/*
 * For signal handlers (sys/signal.c): the current thread's FPU state into
 * `buf' (512 bytes, 16-byte aligned), 0 if it never used the FPU
 */
int x86_fpu_save_to(void *buf)
{
    struct x86_thread *arch = curthread->arch;

    if (!arch->fpu_enabled)
        return 0;

    if (last_fpu_thread == curthread) {
        x86_fpu_enable();
        fpu_save();
        memcpy(buf, fpu_context, 512);
    } else {
        memcpy(buf, arch->fpu_context, 512);
    }

    return 1;
}

/* the current thread starts the FPU afresh at its next use (a handler) */
void x86_fpu_reset(void)
{
    struct x86_thread *arch = curthread->arch;

    arch->fpu_enabled = 0;

    if (last_fpu_thread == curthread)
        last_fpu_thread = NULL;

    x86_fpu_disable();
}

/* sigreturn: the state saved in a frame back (`valid'), loaded at the
 * thread's next use of the FPU; MXCSR as the CPU takes it */
void x86_fpu_load_from(const void *buf, int valid)
{
    struct x86_thread *arch = curthread->arch;

    x86_fpu_reset();

    if (!valid)
        return;

    if (!arch->fpu_context && !(arch->fpu_context = kmalloc(512, &M_X86_FPU, 0)))
        return;

    memcpy(arch->fpu_context, buf, 512);

    if (fpu_fxsr)
        *(uint32_t *) ((char *) arch->fpu_context + 24) &= mxcsr_mask;

    arch->fpu_enabled = 1;
}
