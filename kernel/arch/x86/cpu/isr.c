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
#include <core/string.h>
#include <cpu/cpu.h>
#include <core/arch.h>
#include <sys/sched.h>

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

static const char *int_msg[32] = {
	"Division by zero",				/* 0 */
	"Debug",
	"Non-maskable interrupt",
	"Breakpoint",
	"Detected overflow",
	"Out-of-bounds",				/* 5 */
	"Invalid opcode",
	"No coprocessor",
	"Double fault",
	"Coprocessor segment overrun",
	"Bad TSS",						/* 10 */
	"Segment not present",
	"Stack fault",
	"General protection fault",
	"Page fault",
	"Unknown interrupt",			/* 15 */
	"Coprocessor fault",
	"Alignment check",
	"Machine check",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved"
};

void interrupt(regs_t *regs)
{
	extern uint32_t int_num;
	extern uint32_t err_num;
	
	if (int_num == 0x80) {	/* syscall */
		x86_proc_t *arch = cur_proc->arch;
		arch->regs = regs;
		arch_syscall(regs);
		return;
	}

	const char *msg = int_msg[int_num];
	printk("Recieved interrupt [%d] [err:%d] : %s\n", int_num, err_num, msg);
	printk("Kernel exception\n"); /* That's bad */
	printk("Registers dump:\n");
	printk("edi = %x\n", regs->edi);
	printk("esi = %x\n", regs->esi);
	printk("ebp = %x\n", regs->ebp);
	printk("ebx = %x\n", regs->ebx);
	printk("ecx = %x\n", regs->ecx);
	printk("edx = %x\n", regs->edx);
	printk("eax = %x\n", regs->eax);
	printk("eip = %x\n", regs->eip);
	printk("cs  = %x\n", regs->cs );
	printk("eflags = %x\n", regs->eflags);
	printk("esp = %x\n", regs->esp);
	printk("ss  = %x\n", regs->ss);
	for(;;);
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