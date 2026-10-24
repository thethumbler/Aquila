/** \file
 * x86: the Interrupt Descriptor Table (IDT), which says where each
 * interrupt and exception vector goes. All CPUs share it.
 */

#include <core/system.h>
#include "cpu.h"

#if __pragma_pack
#pragma pack(1)
#endif
static struct idt_entry {
    uint32_t offset_lo : 16;    /* the handler's address, bits 0 to 15 */
    uint32_t selector  : 16;    /* the code segment it runs in */
    uint32_t _unused   : 8;     /* unused, 0 */
    uint32_t flags     : 5;     /* 01110: an interrupt gate */
    uint32_t dpl       : 2;     /* the privilege level that may use int on it */
    uint32_t p         : 1;     /* present */
    uint16_t offset_hi : 16;    /* the handler's address, bits 16 to 31 */
#if ARCH_BITS==64
    uint32_t offset_ext: 32;    /* the handler's address, bits 32 to 63 */
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

/**
 * Sets an interrupt gate that only the hardware (and the kernel) can
 * raise: an exception or a device interrupt. The handler runs in the
 * kernel's code segment, with interrupts disabled.
 *
 * @param id      the vector
 * @param offset  the handler's address
 */
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

/**
 * Sets an interrupt gate that user mode may raise with `int` too: the
 * system call vector. The handler still runs in the kernel's code
 * segment.
 *
 * @param id      the vector
 * @param offset  the handler's address
 */
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

/** Loads the IDT on the running CPU. */
void x86_idt_setup()
{
    idt_pointer.limit = sizeof(idt) - 1;
    idt_pointer.base  = (uintptr_t) &idt;
    x86_lidt((uintptr_t) &idt_pointer);
}
