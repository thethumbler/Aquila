#include <core/system.h>
#include <core/panic.h>
#include <core/assert.h>
#include <core/arch.h>
#include <cpu/cpu.h>
#include <sys/proc.h>
#include <sys/sched.h>

static char fpu_context[512] __aligned(16);
struct thread *last_fpu_thread = NULL;

void x86_fpu_enable(void)
{
    asm volatile("clts");
    write_cr0((read_cr0() & ~CR0_EM) | CR0_MP);
}

void x86_fpu_disable(void)
{
    write_cr0(read_cr0() | CR0_EM);
}

void x86_fpu_init(void)
{
    assert_alignof(fpu_context, 16);
    asm volatile("fninit");
}

static inline void fpu_save(void)
{
    asm volatile("fxsave (%0)"::"r"(fpu_context):"memory");
}

static inline void fpu_restore(void)
{
    asm volatile("fxrstor (%0)"::"r"(fpu_context):"memory");
}

void x86_fpu_trap(void)
{
    x86_fpu_enable();

    struct x86_thread *arch = cur_thread->arch;

    if (!last_fpu_thread) {   /* Initialize */
        x86_fpu_init();
        arch->fpu_enabled = 1;
    } else if (cur_thread != last_fpu_thread) {
        struct x86_thread *_arch = last_fpu_thread->arch;

        if (!_arch->fpu_context)    /* Lazy allocate */
            _arch->fpu_context = kmalloc(512);

        fpu_save();
        memcpy(_arch->fpu_context, fpu_context, 512);

        if (arch->fpu_enabled) {    /* Restore context */
            memcpy(fpu_context, arch->fpu_context, 512);
            fpu_restore();
        } else {
            x86_fpu_init();
            arch->fpu_enabled = 1;
        }
    }

    last_fpu_thread = cur_thread;
}
