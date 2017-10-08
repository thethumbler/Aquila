#include <core/system.h>
#include <core/arch.h>
#include <cpu/cpu.h>
#include <sys/proc.h>
#include <sys/sched.h>

static char fpu_context[512] __aligned(16);
proc_t *last_fpu_proc = NULL;

void enable_fpu()
{
    asm volatile("clts");
    write_cr0((read_cr0() & ~CR0_EM) | CR0_MP);
}

void disable_fpu()
{
    write_cr0(read_cr0() | CR0_EM);
}

void init_fpu()
{
    asm volatile("fninit");
}

static inline void save_fpu()
{
    asm volatile("fxsave (%0)"::"r"(fpu_context):"memory");
}

static inline void restore_fpu()
{
    asm volatile("fxrstor (%0)"::"r"(fpu_context):"memory");
}

void trap_fpu()
{
    enable_fpu();
    x86_proc_t *arch = cur_proc->arch;

    if (!last_fpu_proc) {   /* Initialize */
        init_fpu();
        arch->fpu_enabled = 1;
    } else if (cur_proc != last_fpu_proc) {
        x86_proc_t *_arch = last_fpu_proc->arch;

        if (!_arch->fpu_context)    /* Lazy allocate */
            _arch->fpu_context = kmalloc(512);

        save_fpu();
        memcpy(_arch->fpu_context, fpu_context, 512);

        if (arch->fpu_enabled) {    /* Restore context */
            memcpy(fpu_context, arch->fpu_context, 512);
            restore_fpu();
        } else {
            init_fpu();
            arch->fpu_enabled = 1;
        }
    }

    last_fpu_proc = cur_proc;
}
