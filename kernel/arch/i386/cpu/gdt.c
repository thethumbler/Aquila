/** \file
 * x86: the Global Descriptor Table (GDT) and the TSS.
 *
 * Each CPU has its own GDT: the flat kernel and user code and data
 * segments, which every CPU shares, then its own TSS (the stack that
 * interrupts from user mode switch to) and, on i386, a segment for its
 * `struct cpu`, which `curcpu()` reads through %gs.
 */


#include <core/system.h>
#include <core/panic.h>
#include <core/assert.h>
#include <core/string.h>

#include <cpu/msr.h>
#include <sys/cpu.h>

#include "cpu.h"

#define RW_DATA 0x2
#define XR_CODE 0xA
#define TSS_AVL 0x9

#define BASE  0
#define LIMIT -1

#define DPL0 0
#define DPL3 3

#if ARCH_BITS==32
#define L   0
#define D   1
#else
#define L   1
#define D   0
#endif

/* The segments every CPU has; its TSS and its own segment come after. */
static const struct x86_gdt_entry gdt_template[X86_GDT_TSS] = {
    /* the null segment */
    {0},

    /* the kernel's code segment */
    {LIMIT, BASE, BASE, XR_CODE, 1, DPL0, 1, LIMIT, 0, L, D, 1, BASE},

    /* the kernel's data segment */
    {LIMIT, BASE, BASE, RW_DATA, 1, DPL0, 1, LIMIT, 0, L, D, 1, BASE},

    /* user mode's code segment */
    {LIMIT, BASE, BASE, XR_CODE, 1, DPL3, 1, LIMIT, 0, L, D, 1, BASE},

    /* user mode's data segment */
    {LIMIT, BASE, BASE, RW_DATA, 1, DPL3, 1, LIMIT, 0, L, D, 1, BASE},
};

#define MSR_GS_BASE         0xC0000101
#define MSR_KERNEL_GS_BASE  0xC0000102

/** Sets the base and limit of a system or data segment descriptor, with
 * byte granularity. */
static void segment_set(struct x86_gdt_entry *e, uintptr_t base, size_t limit)
{
    e->limit_lo = limit & 0xFFFF;
    e->base_lo  = base & 0xFFFF;
    e->base_mid = (base >> 16) & 0xFF;
    e->limit_hi = (limit >> 16) & 0xF;
    e->base_hi  = (base >> 24) & 0xFF;
}

/**
 * Sets up and loads the running CPU's GDT and TSS, and points %gs (or the
 * GS base, on x86_64) at its `struct cpu`, so that `curcpu()` works.
 *
 * @param cpu  the running CPU
 * @param sp   the kernel stack that interrupts from user mode switch to
 *             (each thread sets its own later, `x86_kernel_stack_set()`)
 */
void x86_gdt_setup(struct cpu *cpu, uintptr_t sp)
{
    struct arch_cpu *arch = &cpu->arch;
    struct x86_tss *tss = &arch->tss;

    assert_sizeof(struct x86_gdt_entry, 8);
    assert_sizeof(struct x86_tss, 104);
    assert_alignof(arch->gdt, 8);
    assert_alignof(tss, 8);

    memset(arch->gdt, 0, sizeof(arch->gdt));
    memcpy(arch->gdt, gdt_template, sizeof(gdt_template));

    memset(tss, 0, sizeof(*tss));
#if ARCH_BITS==32
    tss->ss = 0x10;
#endif
    tss->sp = sp;

    /*
     * The I/O map base (the upper half of the dword at offset 100) must
     * point past the TSS's limit. Otherwise the TSS's own bytes are used
     * as the I/O permission bitmap, and user mode could use random I/O
     * ports.
     */
#if ARCH_BITS==32
    tss->_[22]  = (uint32_t) sizeof(*tss) << 16;
#else
    tss->__[22] = (uint32_t) sizeof(*tss) << 16;
#endif

    /* the TSS's segment */
    struct x86_gdt_entry *e = &arch->gdt[X86_GDT_TSS];
    segment_set(e, (uintptr_t) tss, sizeof(*tss) - 1);
    e->type = TSS_AVL;
    e->dpl  = DPL3;
    e->p    = 1;

#if ARCH_BITS==64
    uint32_t *base_high = (uint32_t *) &arch->gdt[X86_GDT_TSS + 1];
    *base_high = (uintptr_t) tss >> 32;
#else
    /* the CPU's own segment: %gs:0 is cpu->self */
    e = &arch->gdt[X86_GDT_CPU];
    segment_set(e, (uintptr_t) cpu, sizeof(*cpu) - 1);
    e->type = RW_DATA;
    e->s    = 1;
    e->dpl  = DPL0;
    e->p    = 1;
    e->db   = 1;
#endif

    cpu->self = cpu;

    x86_lgdt(sizeof(arch->gdt) - 1, (uintptr_t) arch->gdt);
    x86_ltr((X86_GDT_TSS << 3) | DPL3);

#if ARCH_BITS==32
    asm volatile("movw %w0, %%gs" :: "r"(X86_SEL_CPU));
#else
    /* the kernel's GS base; user mode's (none yet) is swapped in on the
     * way out to user mode */
    msr_write(MSR_GS_BASE, (uintptr_t) cpu);
    msr_write(MSR_KERNEL_GS_BASE, 0);
#endif
}

/**
 * Sets the kernel stack that interrupts from user mode switch to, on the
 * running CPU: the thread that is about to run's.
 *
 * @param sp  the top of the stack
 */
void x86_kernel_stack_set(uintptr_t sp)
{
    curcpu()->arch.tss.sp = sp;
}
