/**********************************************************************
 *              Programmable Interrupt Controller (PIC)
 *                   (Legacy PIC (8259) support)
 *
 *  -- Should only be used when no APIC is avilable or APIC support 
 *  isn't built into the kernel.
 *
 *  This file is part of AquilaOS and is released under the terms of
 *  GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) Mohamed Anwar
 */

#include <core/system.h>
#include <core/arch.h>
#include <chipset/misc.h>
#include <cpu/cpu.h>

#define PIC_CMD   0x00
#define PIC_DATA  0x01

static struct ioaddr __master;
static struct ioaddr __slave;

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

#define ICW1        0x11 /* Both Master and Slave use the same ICW1 */
#define MASTER_ICW2 0x20 /* Interrupts (from Master) start from offset 32 */
#define SLAVE_ICW2  0x28 /* Interrupts (from Slave)  start from offset 40 */
#define MASTER_ICW3 0x04 /* Master has a Slave attached to IR 2 */
#define SLAVE_ICW3  0x02 /* Slave ID is 2 */
#define ICW4        0x01 /* Sets PIC to 8086 MODE */

/* The mask value currently on slave:master */
static uint16_t pic_mask = 0xFFFF;

void x86_irq_mask(int irq)
{
    if (irq < 8) {  /* Master */
        pic_mask |= 1 << irq;
        io_out8(&__master,  PIC_DATA, (pic_mask >> 8) & 0xFF);
    } else if (irq < 16) {  /* Slave */
        pic_mask |= 1 << irq;
        io_out8(&__slave,  PIC_DATA, (pic_mask >> 8) & 0xFF);
    } else {
        panic("Invalid IRQ number\n");
    }
}

void x86_irq_unmask(int irq)
{
    if (irq < 8) {  /* Master */
        pic_mask &= ~(1 << irq);
        io_out8(&__master,  PIC_DATA, pic_mask & 0xFF);
    } else if (irq < 16) {  /* Slave */
        pic_mask &= ~(1 << irq);
        io_out8(&__slave,  PIC_DATA, (pic_mask >> 8) & 0xFF);
    } else {
        panic("Invalid IRQ number\n");
    }
}

static void x86_irq_remap()
{
    /*
     * Initializes PIC & remaps PIC interrupts to different interrupt
     * numbers so as not to conflict with CPU exceptions
     */

    io_out8(&__master, PIC_CMD,  ICW1);
    io_out8(&__slave,  PIC_CMD,  ICW1);
    io_out8(&__master, PIC_DATA, MASTER_ICW2);
    io_out8(&__slave,  PIC_DATA, SLAVE_ICW2);
    io_out8(&__master, PIC_DATA, MASTER_ICW3);
    io_out8(&__slave,  PIC_DATA, SLAVE_ICW3);
    io_out8(&__master, PIC_DATA, ICW4);
    io_out8(&__slave,  PIC_DATA, ICW4);
}

extern void __x86_irq0 (void);
extern void __x86_irq1 (void);
extern void __x86_irq2 (void);
extern void __x86_irq3 (void);
extern void __x86_irq4 (void);
extern void __x86_irq5 (void);
extern void __x86_irq6 (void);
extern void __x86_irq7 (void);
extern void __x86_irq8 (void);
extern void __x86_irq9 (void);
extern void __x86_irq10(void);
extern void __x86_irq11(void);
extern void __x86_irq12(void);
extern void __x86_irq13(void);
extern void __x86_irq14(void);
extern void __x86_irq15(void);

static x86_irq_handler_t irq_handlers[16] = {0};

void x86_irq_handler_install(unsigned irq, x86_irq_handler_t handler)
{
    if (irq < 16) {
        x86_irq_unmask(irq);
        irq_handlers[irq] = handler;
    }
}

void x86_irq_handler_uninstall(unsigned irq)
{
    if (irq < 16) {
        x86_irq_mask(irq);
        irq_handlers[irq] = (x86_irq_handler_t) NULL;
    }
}

#define IRQ_ACK 0x20
static void x86_irq_ack(uint32_t irq_no)
{
    if (irq_no > 7) /* IRQ fired from the Slave PIC */
        io_out8(&__slave, PIC_CMD, IRQ_ACK);

    io_out8(&__master, PIC_CMD, IRQ_ACK);
}

void __x86_irq_handler(struct x86_regs *r)
{
    extern uint32_t int_num;

    x86_irq_handler_t handler = NULL;

    if (int_num > 47 || int_num < 32) /* Out of range */
        handler = NULL;
    else
        handler = irq_handlers[int_num - 32];

    x86_irq_ack(int_num - 32);

    if (handler) handler(r);
}


static void x86_irq_gates_setup(void)
{
    x86_idt_gate_set(32, (uint32_t) __x86_irq0);
    x86_idt_gate_set(33, (uint32_t) __x86_irq1);
    x86_idt_gate_set(34, (uint32_t) __x86_irq2);
    x86_idt_gate_set(35, (uint32_t) __x86_irq3);
    x86_idt_gate_set(36, (uint32_t) __x86_irq4);
    x86_idt_gate_set(37, (uint32_t) __x86_irq5);
    x86_idt_gate_set(38, (uint32_t) __x86_irq6);
    x86_idt_gate_set(39, (uint32_t) __x86_irq7);
    x86_idt_gate_set(40, (uint32_t) __x86_irq8);
    x86_idt_gate_set(41, (uint32_t) __x86_irq9);
    x86_idt_gate_set(42, (uint32_t) __x86_irq10);
    x86_idt_gate_set(43, (uint32_t) __x86_irq11);
    x86_idt_gate_set(44, (uint32_t) __x86_irq12);
    x86_idt_gate_set(45, (uint32_t) __x86_irq13);
    x86_idt_gate_set(46, (uint32_t) __x86_irq14);
    x86_idt_gate_set(47, (uint32_t) __x86_irq15);
}

int x86_pic_probe()
{
    /* Mask all slave IRQs */
    io_out8(&__slave, PIC_DATA, 0xFF);

    /* Mask all master IRQs -- except slave cascade */
    io_out8(&__master, PIC_DATA, 0xDF);

    /* Check if there is a devices listening to port */
    if (io_in8(&__master, PIC_DATA) != 0xDF)
        return -1;

    return 0;
}

void x86_pic_disable()
{
    /* Done by masking all IRQs */
    io_out8(&__slave,  PIC_DATA, 0xFF);
    io_out8(&__master, PIC_DATA, 0xFF);
}

int x86_pic_setup(struct ioaddr *master, struct ioaddr *slave)
{
    __master = *master;
    __slave  = *slave;

    if (x86_pic_probe()) {
        printk("8259 PIC: Controller not found\n");
        return -1;
    }

    printk("8259 PIC: Initializing [Master: %p (%s), Salve: %p (%s)]\n",
            master->addr, ioaddr_type_str(master),
            slave->addr, ioaddr_type_str(slave));


    /* Initialize */
    x86_irq_remap();

    /* Mask all interrupts */
    x86_pic_disable();

    /* Setup call gates */
    x86_irq_gates_setup();
    return 0;
}
