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
struct thread *last_fpu_thread = NULL;

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
    asm volatile("fninit");
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
