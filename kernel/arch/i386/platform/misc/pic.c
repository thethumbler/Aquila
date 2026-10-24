/**********************************************************************
 *              Programmable Interrupt Controller (PIC)
 *                   (Legacy PIC (8259) support)
 *
 *  An x86_irq_chip (irq.c): the interrupts when there's no APIC (or
 *  apic=off). With the APIC, they are remapped (vectors 32 to 47) and
 *  masked, they stay so.
 */

#include <core/system.h>
#include <core/panic.h>

#include <core/arch.h>
#include <platform/misc.h>
#include <sys/sched.h>
#include <sys/signal.h>
#include <cpu/cpu.h>

#define PIC_CMD   0x00
#define PIC_DATA  0x01

static struct ioaddr master;
static struct ioaddr slave;

/**
 * \file pic.c
 * ```
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
 *              0= EDGE TRIGERED MODE
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
 *```
 */

#define ICW1        0x11 /* Both Master and Slave use the same ICW1 */
#define ICW2_MASTER 0x20 /* Interrupts (from Master) start from offset 32 */
#define ICW2_SLAVE  0x28 /* Interrupts (from Slave)  start from offset 40 */
#define ICW3_MASTER 0x04 /* Master has a Slave attached to IR 2 */
#define ICW3_SLAVE  0x02 /* Slave ID is 2 */
#define ICW4        0x01 /* Sets PIC to 8086 MODE */

/* The mask value currently on slave:master */
static uint16_t pic_mask = 0xFFFF;

static void x86_irq_mask(int irq)
{
    if (irq < 8) {  /* Master */
        pic_mask |= 1 << irq;
        io_out8(&master,  PIC_DATA, pic_mask & 0xFF);
    } else if (irq < 16) {  /* Slave */
        pic_mask |= 1 << irq;
        io_out8(&slave,  PIC_DATA, (pic_mask >> 8) & 0xFF);
    } else {
        panic("Invalid IRQ number\n");
    }
}

static void x86_irq_unmask(int irq)
{
    if (irq < 8) {  /* Master */
        pic_mask &= ~(1 << irq);
        io_out8(&master,  PIC_DATA, pic_mask & 0xFF);
    } else if (irq < 16) {  /* Slave */
        pic_mask &= ~(1 << irq);
        pic_mask &= ~(1 << 2);  /* Unmask slave cascade on master */
        io_out8(&slave,  PIC_DATA, (pic_mask >> 8) & 0xFF);
        io_out8(&master, PIC_DATA, pic_mask & 0xFF);
    } else {
        panic("Invalid IRQ number\n");
    }
}

static void x86_irq_remap(void)
{
    /*
     * Initializes PIC & remaps PIC interrupts to different interrupt
     * numbers so as not to conflict with CPU exceptions
     */

    io_out8(&master, PIC_CMD,  ICW1);
    io_out8(&slave,  PIC_CMD,  ICW1);
    io_out8(&master, PIC_DATA, ICW2_MASTER);
    io_out8(&slave,  PIC_DATA, ICW2_SLAVE);
    io_out8(&master, PIC_DATA, ICW3_MASTER);
    io_out8(&slave,  PIC_DATA, ICW3_SLAVE);
    io_out8(&master, PIC_DATA, ICW4);
    io_out8(&slave,  PIC_DATA, ICW4);
}

#define IRQ_ACK 0x20

static void pic_chip_eoi(unsigned irq)
{
    if (irq >= 16)
        return;

    if (irq > 7) /* IRQ fired from the Slave PIC */
        io_out8(&slave, PIC_CMD, IRQ_ACK);

    io_out8(&master, PIC_CMD, IRQ_ACK);
}

static void pic_chip_mask(unsigned irq)
{
    if (irq < 16)
        x86_irq_mask(irq);
}

static void pic_chip_unmask(unsigned irq)
{
    if (irq < 16)
        x86_irq_unmask(irq);
}

static const char *pic_chip_describe(unsigned irq)
{
    return irq < 16 ? "8259 edge" : "";
}

struct x86_irq_chip x86_pic_chip = {
    .name     = "8259",
    .mask     = pic_chip_mask,
    .unmask   = pic_chip_unmask,
    .eoi      = pic_chip_eoi,
    .describe = pic_chip_describe,
};

static int x86_pic_probe(void)
{
    /* Mask all slave IRQs */
    io_out8(&slave, PIC_DATA, 0xFF);

    /* Mask all master IRQs -- except slave cascade */
    io_out8(&master, PIC_DATA, 0xFB);

    /* Check if there is a devices listening to port */
    if (io_in8(&master, PIC_DATA) != 0xFB)
        return -ENODEV;

    return 0;
}

void x86_pic_disable(void)
{
    /* Done by masking all IRQs */
    io_out8(&slave,  PIC_DATA, 0xFF);
    io_out8(&master, PIC_DATA, 0xFF);
}

int x86_pic_setup(struct ioaddr *_master, struct ioaddr *_slave)
{
    master = *_master;
    slave  = *_slave;

    if (x86_pic_probe()) {
        printk("i8259: Controller not found\n");
        return -ENODEV;
    }

    printk("i8259: initializing [Master: %p (%s), Salve: %p (%s)]\n",
            master.addr, ioaddr_type_str(&master),
            slave.addr, ioaddr_type_str(&slave));

    /* Initialize */
    x86_irq_remap();

    /* Mask all interrupts */
    x86_pic_disable();
    return 0;
}
