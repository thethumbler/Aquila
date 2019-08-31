/**********************************************************************
 *              Interrupt Service Routines (ISRs)
 *
 *
 *  This file is part of AquilaOS and is released under the terms of
 *  GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) Mohamed Anwar
 */

#include <core/system.h>
#include <core/arch.h>

#include <core/panic.h>
#include <core/string.h>
#include <cpu/cpu.h>
#include <sys/sched.h>
#include <mm/mm.h>

extern void __x86_isr0 (void);
extern void __x86_isr1 (void);
extern void __x86_isr2 (void);
extern void __x86_isr3 (void);
extern void __x86_isr4 (void);
extern void __x86_isr5 (void);
extern void __x86_isr6 (void);
extern void __x86_isr7 (void);
extern void __x86_isr8 (void);
extern void __x86_isr9 (void);
extern void __x86_isr10(void);
extern void __x86_isr11(void);
extern void __x86_isr12(void);
extern void __x86_isr13(void);
extern void __x86_isr14(void);
extern void __x86_isr15(void);
extern void __x86_isr16(void);
extern void __x86_isr17(void);
extern void __x86_isr18(void);
extern void __x86_isr19(void);
extern void __x86_isr20(void);
extern void __x86_isr21(void);
extern void __x86_isr22(void);
extern void __x86_isr23(void);
extern void __x86_isr24(void);
extern void __x86_isr25(void);
extern void __x86_isr26(void);
extern void __x86_isr27(void);
extern void __x86_isr28(void);
extern void __x86_isr29(void);
extern void __x86_isr30(void);
extern void __x86_isr31(void);
extern void __x86_isr128(void);

/* Refer to 
 * - Intel 64 and IA-32 Architectures Software Developer’s Manual
 * - Volume 3: System Programming Guide
 * - Table 6-1. Protected-Mode Exceptions and Interrupts
 */

static const char *int_msg[32] = {
    /* 0x00 */ "#DE: Divide Error",
    /* 0x01 */ "#DB: Debug Exception",
    /* 0x02 */ "NMI Interrupt",
    /* 0x03 */ "#BP: Breakpoint",
    /* 0x04 */ "#OF: Overflow",
    /* 0x05 */ "#BR: BOUND Range Exceeded",
    /* 0x06 */ "#UD: Invalid Opcode (Undefined Opcode)",
    /* 0x07 */ "#NM: Device Not Available (No Math Coprocessor)",
    /* 0x08 */ "#DF: Double Fault",
    /* 0x09 */ "Coprocessor Segment Overrun (reserved)",
    /* 0x0a */ "#TS: Invalid TSS",
    /* 0x0b */ "#NP: Segment Not Present",
    /* 0x0C */ "#SS: Stack-Segment Fault",
    /* 0x0D */ "#GP: General Protection",
    /* 0x0E */ "#PF: Page Fault",
    /* 0x0F */ "Reserved",
    /* 0x10 */ "#MF: x87 FPU Floating-Point Error (Math Fault)",
    /* 0x11 */ "#AC: Alignment Check",
    /* 0x12 */ "#MC: Machine Check",
    /* 0x13 */ "#XM: SIMD Floating-Point Exception",
    /* 0x14 */ "#VE: Virtualization Exception",
    /* 0x15 */ "Reserved",
    /* 0x16 */ "Reserved",
    /* 0x17 */ "Reserved",
    /* 0x18 */ "Reserved",
    /* 0x19 */ "Reserved",
    /* 0x1A */ "Reserved",
    /* 0x1B */ "Reserved",
    /* 0x1C */ "Reserved",
    /* 0x1D */ "Reserved",
    /* 0x1E */ "Reserved",
    /* 0x1F */ "Reserved"
};

void __x86_isr(struct x86_regs *regs)
{
    extern uint32_t __x86_isr_int_num;
    extern uint32_t __x86_isr_err_num;

    if (__x86_isr_int_num == 0xE && cur_thread) { /* Page Fault */
        struct x86_thread *arch = cur_thread->arch;
        //arch->regs = regs;

#if ARCH_BITS==32
        if (regs->eip == 0x0FFF) {  /* Signal return */
#else
        if (regs->rip == 0x0FFF) {  /* Signal return */
#endif
            /* Fix kstack and regs pointers*/
            arch->regs = (struct x86_regs *) arch->kstack;
            arch->kstack += sizeof(struct x86_regs); 
            x86_kernel_stack_set(arch->kstack);

            extern void return_from_signal(uintptr_t) __attribute__((noreturn));
            return_from_signal((uintptr_t) arch->regs);
        }

#if 0
        const char *name = cur_thread->owner->name;
        if (name && !strcmp(name, "/bin/fbterm")) {
            x86_dump_registers(regs);
            printk("page = %p\n", read_cr2());
            for (;;);
        }
#endif

        //x86_dump_registers(regs);
        uintptr_t addr = read_cr2();

        arch_mm_page_fault(addr, __x86_isr_err_num);
        return;
    }

    if (__x86_isr_int_num == 0x07) {  /* FPU Trap */
        x86_fpu_trap();
        return;
    }
    
    if (__x86_isr_int_num == 0x80) {  /* syscall */
        struct x86_thread *arch = cur_thread->arch;
        arch->regs = regs;
        //asm volatile ("sti");
        arch_syscall(regs);
        return;
    }


    if (__x86_isr_int_num < 32) {
        const char *msg = int_msg[__x86_isr_int_num];
        printk("Recieved interrupt %d [err=%d]: %s\n", __x86_isr_int_num, __x86_isr_err_num, msg);

        if (__x86_isr_int_num == 0x0E) { /* Page Fault */
            printk("CR2 = %p\n", read_cr2());
        }
        x86_dump_registers(regs);
        panic("Kernel Exception");
    } else {
        printk("Unhandled interrupt %d\n", __x86_isr_int_num);
        panic("Kernel Exception");
    }
}

void x86_isr_setup(void)
{   
    x86_idt_gate_set(0x00, (uintptr_t) __x86_isr0);
    x86_idt_gate_set(0x01, (uintptr_t) __x86_isr1);
    x86_idt_gate_set(0x02, (uintptr_t) __x86_isr2);
    x86_idt_gate_set(0x03, (uintptr_t) __x86_isr3);
    x86_idt_gate_set(0x04, (uintptr_t) __x86_isr4);
    x86_idt_gate_set(0x05, (uintptr_t) __x86_isr5);
    x86_idt_gate_set(0x06, (uintptr_t) __x86_isr6);
    x86_idt_gate_set(0x07, (uintptr_t) __x86_isr7);
    x86_idt_gate_set(0x08, (uintptr_t) __x86_isr8);
    x86_idt_gate_set(0x09, (uintptr_t) __x86_isr9);
    x86_idt_gate_set(0x0A, (uintptr_t) __x86_isr10);
    x86_idt_gate_set(0x0B, (uintptr_t) __x86_isr11);
    x86_idt_gate_set(0x0C, (uintptr_t) __x86_isr12);
    x86_idt_gate_set(0x0D, (uintptr_t) __x86_isr13);
    x86_idt_gate_set(0x0E, (uintptr_t) __x86_isr14);
    x86_idt_gate_set(0x0F, (uintptr_t) __x86_isr15);
    x86_idt_gate_set(0x10, (uintptr_t) __x86_isr16);
    x86_idt_gate_set(0x11, (uintptr_t) __x86_isr17);
    x86_idt_gate_set(0x12, (uintptr_t) __x86_isr18);
    x86_idt_gate_set(0x13, (uintptr_t) __x86_isr19);
    x86_idt_gate_set(0x14, (uintptr_t) __x86_isr20);
    x86_idt_gate_set(0x15, (uintptr_t) __x86_isr21);
    x86_idt_gate_set(0x16, (uintptr_t) __x86_isr22);
    x86_idt_gate_set(0x17, (uintptr_t) __x86_isr23);
    x86_idt_gate_set(0x18, (uintptr_t) __x86_isr24);
    x86_idt_gate_set(0x19, (uintptr_t) __x86_isr25);
    x86_idt_gate_set(0x1A, (uintptr_t) __x86_isr26);
    x86_idt_gate_set(0x1B, (uintptr_t) __x86_isr27);
    x86_idt_gate_set(0x1C, (uintptr_t) __x86_isr28);
    x86_idt_gate_set(0x1D, (uintptr_t) __x86_isr29);
    x86_idt_gate_set(0x1E, (uintptr_t) __x86_isr30);
    x86_idt_gate_set(0x1F, (uintptr_t) __x86_isr31);
    x86_idt_gate_user_set(0x80, (uintptr_t) __x86_isr128);
}
