/**********************************************************************
 *              Global Descriptor Table (GDT)
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

/* the segments every CPU has, the TSS and its own after them */
static const struct x86_gdt_entry gdt_template[X86_GDT_TSS] = {
    /* Null Segment */
    {0},

    /* Code Segment - Kernel */
    {LIMIT, BASE, BASE, XR_CODE, 1, DPL0, 1, LIMIT, 0, L, D, 1, BASE},

    /* Data Segment - Kernel */
    {LIMIT, BASE, BASE, RW_DATA, 1, DPL0, 1, LIMIT, 0, L, D, 1, BASE},

    /* Code Segment - User */
    {LIMIT, BASE, BASE, XR_CODE, 1, DPL3, 1, LIMIT, 0, L, D, 1, BASE},

    /* Data Segment - User */
    {LIMIT, BASE, BASE, RW_DATA, 1, DPL3, 1, LIMIT, 0, L, D, 1, BASE},
};

#define MSR_GS_BASE         0xC0000101
#define MSR_KERNEL_GS_BASE  0xC0000102

/* a system or data segment's base and limit (byte granular) */
static void segment_set(struct x86_gdt_entry *e, uintptr_t base, size_t limit)
{
    e->limit_lo = limit & 0xFFFF;
    e->base_lo  = base & 0xFFFF;
    e->base_mid = (base >> 16) & 0xFF;
    e->limit_hi = (limit >> 16) & 0xF;
    e->base_hi  = (base >> 24) & 0xFF;
}

/*
 * The running CPU's GDT, `cpu''s: loaded, with its TSS (interrupts from
 * user mode take the stack `sp'), and %gs (curcpu()) set to `cpu'
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
     * I/O map base (upper half of the dword at offset 100) must point past
     * the TSS limit, otherwise the TSS bytes themselves are used as I/O
     * permission bitmap and ring 3 is granted access to random I/O ports
     */
#if ARCH_BITS==32
    tss->_[22]  = (uint32_t) sizeof(*tss) << 16;
#else
    tss->__[22] = (uint32_t) sizeof(*tss) << 16;
#endif

    /* TSS Segment */
    struct x86_gdt_entry *e = &arch->gdt[X86_GDT_TSS];
    segment_set(e, (uintptr_t) tss, sizeof(*tss) - 1);
    e->type = TSS_AVL;
    e->dpl  = DPL3;
    e->p    = 1;

#if ARCH_BITS==64
    uint32_t *base_high = (uint32_t *) &arch->gdt[X86_GDT_TSS + 1];
    *base_high = (uintptr_t) tss >> 32;
#else
    /* the CPU's segment: %gs:0 is cpu->self */
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
    /* the kernel's in GS base; user mode's (none yet) swapped in on the
     * way out */
    msr_write(MSR_GS_BASE, (uintptr_t) cpu);
    msr_write(MSR_KERNEL_GS_BASE, 0);
#endif
}

void x86_kernel_stack_set(uintptr_t sp)
{
    curcpu()->arch.tss.sp = sp;
}
