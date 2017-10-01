/**********************************************************************
 *				Programmable Interrupt Controller (PIC)
 *					 (Legacy PIC (8259) support)
 *
 *	-- Should only be used when no APIC is avilable or APIC support 
 *  isn't built into the kernel.
 *
 *	This file is part of Aquila OS and is released under the terms of
 *	GNU GPLv3 - See LICENSE.
 *
 *	Copyright (C) 2016 Mohamed Anwar <mohamed_anwar@opmbx.org>
 */

#include <core/system.h>
#include <cpu/cpu.h>
#include <cpu/io.h>

#define MASTER_PIC_CMD	0x20
#define MASTER_PIC_DATA	0x21
#define SLAVE_PIC_CMD	0xA0
#define SLAVE_PIC_DATA	0xA1

/*
 * ICW1 (Sent on COMMAND port of each PIC)
 *
 * | A7 | A6 | A5 | 1 | LTIM | ADI | SINGL | IC4 |
 *   |____|____|          |     |     |       |______ 1=ICW4 REQUIRED
 *        |               |     |   1=SINGEL          0=ICW4 NOT REQUIRED
 *  A7:5 OF INTERRUPT     |     |   0=CASCADED
 *   VECTOR ADDRESS       |     |
 * (MCS-80/85 MODE ONLY!) | CALL ADDRESS INTERVAL (IGNORED IN 8086 MODE)
 *                        | 1=INTERVAL OF 4
 *                        | 0=INTERVAL OF 8
 *                        |
 *              1=LEVEL TRIGERED MODE
 *              0=EDGE  TRIGERED MODE
 *
 *******************************************************************************
 * ICW2 (Sent on DATA port of each PIC)
 *
 * | A15 | A14 | A13 | A12 | A11 | A10 | A9 | A8 |
 *   |_____|_____|_____|_____|_____|_____|____|
 *                        |
 *     A15:8 OF INTERRUPT VECTOR ADDRESS (MCS-80/85 MODE)
 *     T7:3  OF INTERRUPT VECTOR ADDRESS (8086 MODE)
 *
 *******************************************************************************
 * ICW3 (Sent on DATA port of each PIC)
 *
 * --FOR MASTER:
 * | S7 | S6 | S5 | S4 | S3 | S2 | S1 | S0 |
 *   |____|____|____|____|____|____|____|
 *                  |
 *       1=IR LINE HAS SLAVE (CASCADED)
 *       0=IR LINE DOES NOT HAVE SLAVE (SINGLE)
 *
 * --FOR SLAVE:
 * | 0 | 0 | 0 | 0 | 0 | ID2 | ID1 | ID0 |
 *                       |_____|_____|
 *                             |
 *                         SLAVE ID
 *
 *******************************************************************************
 * ICW4 (Sent on DATA port of each PIC)
 * Well, I am too lazy to write this one XD so I will just tell you that setting
 * the least-significant bit sets the PIC to 8086 MODE
 *
 */

#define ICW1		0x11 /* Both Master and Slave use the same ICW1 */
#define MASTER_ICW2	0x20 /* Interrupts (from Master) start from offset 32 */
#define SLAVE_ICW2	0x28 /* Interrupts (from Slave)  start from offset 40 */
#define MASTER_ICW3	0x04 /* Master has a Slave attached to IR 2 */
#define SLAVE_ICW3	0x02 /* Slave ID is 2 */
#define ICW4		0x01 /* Sets PIC to 8086 MODE */

static void irq_remap()
{
	/*
	 * Remaps PIC interrupts to different interrupts numbers so as not to
	 * conflict with CPU exceptions
	 */

	outb(MASTER_PIC_CMD,  ICW1);
	outb(SLAVE_PIC_CMD,   ICW1);
	outb(MASTER_PIC_DATA, MASTER_ICW2);
	outb(SLAVE_PIC_DATA,  SLAVE_ICW2);
	outb(MASTER_PIC_DATA, MASTER_ICW3);
	outb(SLAVE_PIC_DATA,  SLAVE_ICW3);
	outb(MASTER_PIC_DATA, ICW4);
	outb(SLAVE_PIC_DATA,  ICW4);
}

extern void irq0 (void);
extern void irq1 (void);
extern void irq2 (void);
extern void irq3 (void);
extern void irq4 (void);
extern void irq5 (void);
extern void irq6 (void);
extern void irq7 (void);
extern void irq8 (void);
extern void irq9 (void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

static irq_handler_t irq_handlers[16] = {0};

void irq_install_handler(unsigned irq, irq_handler_t handler)
{
	if (irq < 16)
		irq_handlers[irq] = handler;
}

void irq_uninstall_handler(unsigned irq)
{
	if (irq < 16)
		irq_handlers[irq] = (irq_handler_t) NULL;
}

#define IRQ_ACK	0x20
void irq_ack(uint32_t irq_no)
{
	if (irq_no > 7)	/* IRQ fired from the Slave PIC */
		outb(SLAVE_PIC_CMD, IRQ_ACK);

	outb(MASTER_PIC_CMD, IRQ_ACK);
}

void irq_handler(regs_t *r)
{
	extern uint32_t int_num;
	/* extern uint32_t err_num; */
    //printk("irq_handler(%d)\n", int_num);

	irq_handler_t handler = NULL;

	if (int_num > 47 || int_num < 32) /* Out of range */
		handler = NULL;
	else
		handler = irq_handlers[int_num - 32];

    irq_ack(int_num - 32);

	if (handler) handler(r);
}


static void irq_setup_gates(void)
{
	idt_set_gate(32, (uint32_t) irq0);
	idt_set_gate(33, (uint32_t) irq1);
	idt_set_gate(34, (uint32_t) irq2);
	idt_set_gate(35, (uint32_t) irq3);
	idt_set_gate(36, (uint32_t) irq4);
	idt_set_gate(37, (uint32_t) irq5);
	idt_set_gate(38, (uint32_t) irq6);
	idt_set_gate(39, (uint32_t) irq7);
	idt_set_gate(40, (uint32_t) irq8);
	idt_set_gate(41, (uint32_t) irq9);
	idt_set_gate(42, (uint32_t) irq10);
	idt_set_gate(43, (uint32_t) irq11);
	idt_set_gate(44, (uint32_t) irq12);
	idt_set_gate(45, (uint32_t) irq13);
	idt_set_gate(46, (uint32_t) irq14);
	idt_set_gate(47, (uint32_t) irq15);
}

void pic_setup()
{
	irq_remap();
	irq_setup_gates();
}

void pic_disable()
{
	/* Done by masking all IRQs */
	outb(SLAVE_PIC_DATA, 0xFF);
	outb(MASTER_PIC_DATA, 0xFF);
}
