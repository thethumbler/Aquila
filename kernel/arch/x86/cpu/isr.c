/**********************************************************************
 *				Interrupt Service Routines (ISRs)
 *
 *
 *	This file is part of Aquila OS and is released under the terms of
 *	GNU GPLv3 - See LICENSE.
 *
 *	Copyright (C) 2016 Mohamed Anwar <mohamed_anwar@opmbx.org>
 */

#include <core/system.h>
#include <core/panic.h>
#include <core/string.h>
#include <cpu/cpu.h>
#include <core/arch.h>
#include <sys/sched.h>
#include <mm/mm.h>

extern void isr0 (void);
extern void isr1 (void);
extern void isr2 (void);
extern void isr3 (void);
extern void isr4 (void);
extern void isr5 (void);
extern void isr6 (void);
extern void isr7 (void);
extern void isr8 (void);
extern void isr9 (void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);
extern void isr128(void);

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

void interrupt(regs_t *regs)
{
	extern uint32_t int_num;
	extern uint32_t err_num;

    //x86_dump_registers(regs);

    if (int_num == 0xE && cur_proc && regs->cs == X86_CS) {   /* Page fault from user-space */

        if (regs->eip == 0x0FFF) {  /* Signal return */
            //printk("Returned from signal [regs=%p]\n", regs);
            x86_proc_t *arch = cur_proc->arch;

            /* Fix kstack and regs pointers*/
            arch->regs = (regs_t *) arch->kstack;
            arch->kstack += sizeof(regs_t); 
            set_kernel_stack(arch->kstack);

            extern void return_from_signal(uintptr_t) __attribute__((noreturn));
            return_from_signal((uintptr_t) arch->regs);
        }

        pmman.handle_page_fault(read_cr2());
        return;
    }

    if (int_num == 0x07) {  /* FPU Trap */
        trap_fpu();
        return;
    }
	
	if (int_num == 0x80) {	/* syscall */
		x86_proc_t *arch = cur_proc->arch;
		arch->regs = regs;
		arch_syscall(regs);
		return;
	}


    if (int_num < 32) {
        const char *msg = int_msg[int_num];
        printk("Recieved interrupt %d [err=%d]: %s\n", int_num, err_num, msg);
        if (int_num == 0x0E) { /* Page Fault */
            printk("CR2 = %p\n", read_cr2());
        }
        x86_dump_registers(regs);
        panic("Kernel Exception");
    } else {
        printk("Unhandled interrupt %d\n", int_num);
        panic("Kernel Exception");
    }
}

void isr_setup()
{	
	idt_set_gate(0x00, (uint32_t) isr0);
	idt_set_gate(0x01, (uint32_t) isr1);
	idt_set_gate(0x02, (uint32_t) isr2);
	idt_set_gate(0x03, (uint32_t) isr3);
	idt_set_gate(0x04, (uint32_t) isr4);
	idt_set_gate(0x05, (uint32_t) isr5);
	idt_set_gate(0x06, (uint32_t) isr6);
	idt_set_gate(0x07, (uint32_t) isr7);
	idt_set_gate(0x08, (uint32_t) isr8);
	idt_set_gate(0x09, (uint32_t) isr9);
	idt_set_gate(0x0A, (uint32_t) isr10);
	idt_set_gate(0x0B, (uint32_t) isr11);
	idt_set_gate(0x0C, (uint32_t) isr12);
	idt_set_gate(0x0D, (uint32_t) isr13);
	idt_set_gate(0x0E, (uint32_t) isr14);
	idt_set_gate(0x0F, (uint32_t) isr15);
	idt_set_gate(0x10, (uint32_t) isr16);
	idt_set_gate(0x11, (uint32_t) isr17);
	idt_set_gate(0x12, (uint32_t) isr18);
	idt_set_gate(0x13, (uint32_t) isr19);
	idt_set_gate(0x14, (uint32_t) isr20);
	idt_set_gate(0x15, (uint32_t) isr21);
	idt_set_gate(0x16, (uint32_t) isr22);
	idt_set_gate(0x17, (uint32_t) isr23);
	idt_set_gate(0x18, (uint32_t) isr24);
	idt_set_gate(0x19, (uint32_t) isr25);
	idt_set_gate(0x1A, (uint32_t) isr26);
	idt_set_gate(0x1B, (uint32_t) isr27);
	idt_set_gate(0x1C, (uint32_t) isr28);
	idt_set_gate(0x1D, (uint32_t) isr29);
	idt_set_gate(0x1E, (uint32_t) isr30);
	idt_set_gate(0x1F, (uint32_t) isr31);
	idt_set_gate_user(0x80, (uint32_t) isr128);
}
