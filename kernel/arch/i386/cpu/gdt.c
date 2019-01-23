/**********************************************************************
 *              Global Descriptor Table (GDT)
 *
 *
 *  This file is part of Aquila OS and is released under the terms of
 *  GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) Mohamed Anwar
 */


#include <core/system.h>
#include <core/panic.h>
#include <core/assert.h>
#include <core/string.h>

#include "cpu.h"

#if ARCH_BITS==32
static struct {
    uint32_t link;
    uint32_t sp;
    uint32_t ss;
    uint32_t _[23]; /* To know the actuall fields, consult Intel Manuals */
} __packed tss_entry __aligned(8);
#else
static struct {
    uint32_t _;
    uint64_t sp;
    uint32_t __[23]; /* To know the actuall fields, consult Intel Manuals */
} __packed tss_entry __aligned(8);
#endif

#define TSS_BASE    ((uintptr_t) &tss_entry)
#define TSS_LIMIT   (sizeof(tss_entry) - 1)

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

static struct gdt_entry {
    uint32_t limit_lo : 16; /* Segment Limit 15:00 */
    uint32_t base_lo  : 16; /* Base Address 15:00 */

    uint32_t base_mid : 8;  /* Base Address 23:16 */
    uint32_t type     : 4;  /* Segment Type */
    uint32_t s        : 1;  /* Descriptor type (0=system, 1=code) */
    uint32_t dpl      : 2;  /* Descriptor Privellage Level */
    uint32_t p        : 1;  /* Segment present */

    uint32_t limit_hi : 4;  /* Segment Limit 19:16 */
    uint32_t avl      : 1;  /* Avilable for use by system software */
    uint32_t l        : 1;  /* Long mode segment (64-bit only) */
    uint32_t db       : 1;  /* Default operation size / upper Bound */
    uint32_t g        : 1;  /* Granularity */
    uint32_t base_hi  : 8;  /* Base Address 31:24 */
} __aligned(8) gdt[256] = {
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

void x86_gdt_setup(void)
{
    assert_sizeof(gdt, 8*256);
    assert_alignof(&gdt, 8);
    x86_lgdt(sizeof(gdt) - 1, (uintptr_t) &gdt);
}

void x86_tss_setup(uintptr_t sp)
{
    assert_sizeof(tss_entry, 104);
    assert_alignof(&tss_entry, 8);

#if ARCH_BITS==32
    tss_entry.ss = 0x10;
#endif
    tss_entry.sp = sp;

    /* TSS Segment */
#if 0
    gdt[5] = (struct gdt_entry){TSS_LIMIT & 0xFFFF, TSS_BASE & 0xFFFF,
        (TSS_BASE >> 16) & 0xFF, TSS_AVL, 0, DPL3, 1,
        (TSS_LIMIT >> 16) & 0xF, 0, 0, 0, 0, (TSS_BASE >> 24 & 0xFF)};
#endif
    gdt[5].limit_lo = TSS_LIMIT & 0xFFFF;
    gdt[5].base_lo  = TSS_BASE & 0xFFFF;
    gdt[5].base_mid = (TSS_BASE >> 16) & 0xFF;
    gdt[5].type     = TSS_AVL;
    gdt[5].s        = 0;
    gdt[5].dpl      = DPL3;
    gdt[5].p        = 1;
    gdt[5].limit_hi = (TSS_LIMIT >> 16) & 0xF;
    gdt[5].avl      = 0;
    gdt[5].l        = 0;
    gdt[5].db       = 0;
    gdt[5].g        = 0;
    gdt[5].base_hi  = (TSS_BASE >> 24 & 0xFF);

#if ARCH_BITS==64
    uint32_t *base_high = (uint32_t *) &gdt[6];
    *base_high = TSS_BASE >> 32;
#endif

    x86_ltr(0x28 | DPL3);
}

void x86_kernel_stack_set(uintptr_t sp)
{
    tss_entry.sp = sp;
}
