/**********************************************************************
 *              Interrupt Descriptor Table (IDT)
 *
 *
 *  This file is part of Aquila OS and is released under the terms of
 *  GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) Mohamed Anwar 
 */

#include <core/system.h>
#include "sys.h"

#if __pragma_pack
#pragma pack(1)
#endif
static struct idt_entry {
    uint32_t offset_lo : 16;    /* offset 0:15 */
    uint32_t selector  : 16;    /* Code segment selector */
    uint32_t _unused   : 8;     /* Unused, should be set to 0 */
    uint32_t flags     : 5;     /* Always set to 01110 */
    uint32_t dpl       : 2;     /* Descriptor privellage level */
    uint32_t p         : 1;     /* Present */
    uint16_t offset_hi : 16;    /* offset 16:31 */
#if ARCH_BITS==64
    uint32_t offset_ext: 32;    /* offset 32:63 */
    uint32_t           : 32;    /* reserved */
#endif
} __packed idt[256];

struct idt_ptr {
    uint16_t limit;
    uintptr_t base;
} __packed idt_pointer;
#if __pragma_pack
#pragma pack()
#endif

#define DPL0 0
#define DPL3 3

/* Sets Interrupt gates in Kernel Code Segment */
void x86_idt_gate_set(uint32_t id, uintptr_t offset)
{
    idt[id].offset_lo  = (offset >> 0x00) & 0xFFFF;
    idt[id].offset_hi  = (offset >> 0x10) & 0xFFFF;
#if ARCH_BITS==64
    idt[id].offset_ext = (offset >> 0x20) & 0xFFFFFFFF;
#endif

    idt[id].selector   = 0x8;
    idt[id].p          = 1;
    idt[id].dpl        = DPL0;
    idt[id].flags      = 0x0E;
}

/* Sets Interrupt gates in User Code Segment */
void x86_idt_gate_user_set(uint32_t id, uintptr_t offset)
{
    idt[id].offset_lo  = (offset >> 0x00) & 0xFFFF;
    idt[id].offset_hi  = (offset >> 0x10) & 0xFFFF;
#if ARCH_BITS==64
    idt[id].offset_ext = (offset >> 0x20) & 0xFFFFFFFF;
#endif

    idt[id].selector   = 0x8;
    idt[id].p          = 1;
    idt[id].dpl        = DPL3;
    idt[id].flags      = 0x0E;
}

void x86_idt_setup()
{
    idt_pointer.limit = sizeof(idt) - 1;
    idt_pointer.base  = (uintptr_t) &idt;
    x86_lidt((uintptr_t) &idt_pointer);
}
