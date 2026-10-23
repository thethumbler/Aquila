#include <core/system.h>
#include <core/string.h>
#include <core/kargs.h>
#include <cpu/cpu.h>
#include <cpu/io.h>
#include <platform/misc.h>

static struct ioaddr i8042_ioaddr;

static void (*channel1_handler)(int) = NULL;
static void (*channel2_handler)(int) = NULL;

#define FIRST_IRQ        1
#define SECOND_IRQ       12

#define DATA_PORT        0x00
#define STATUS_PORT      0x04
#define COMMAND_PORT     0x04

#define STATUS_OBUF_FULL 0x01
#define STATUS_IBUF_FULL 0x02
#define STATUS_SYSTEM    0x04
#define STATUS_COMMAND   0x08
#define STATUS_TOUT      0x40
#define STATUS_PARITY    0x80

static uint8_t read_status(void)
{
    return io_in8(&i8042_ioaddr, STATUS_PORT);
}

/* the controller has a byte for us (its output buffer is full): the
 * interrupt comes when it has, there is nothing to wait for; one with
 * nothing to read is spurious */
static int has_byte(void)
{
    return read_status() & STATUS_OBUF_FULL;
}

static void x86_i8042_first_handler(struct x86_regs *r)
{
    (void) r;

    if (!has_byte())
        return;

    int scancode = io_in8(&i8042_ioaddr, DATA_PORT);

    if (channel1_handler)
        channel1_handler(scancode);
}

static void x86_i8042_second_handler(struct x86_regs *r)
{
    (void) r;

    if (!has_byte())
        return;

    int scancode = io_in8(&i8042_ioaddr, DATA_PORT);

    if (channel2_handler)
        channel2_handler(scancode);
}

static void x86_i8042_handler_install(void)
{
    printk("i8042: installing IRQ %d\n", FIRST_IRQ);
    x86_irq_handler_install(FIRST_IRQ, x86_i8042_first_handler);

    printk("i8042: installing IRQ %d\n", SECOND_IRQ);
    x86_irq_handler_install(SECOND_IRQ, x86_i8042_second_handler);

    /* flush any pending data */
    for (int i = 0; i < 10; ++i)
        io_in8(&i8042_ioaddr, DATA_PORT);
}

void x86_i8042_handler_register(int channel, void (*fun)(int))
{
    switch (channel) {
        case 1:
            channel1_handler = fun;
            break;
        case 2:
            channel2_handler = fun;
            break;
    }
}

/* the controller takes a byte, -1 if it doesn't in time */
static int write_wait(void)
{
    for (int i = 0; i < 100000; ++i)
        if (!(read_status() & STATUS_IBUF_FULL))
            return 0;

    return -1;
}

/* a byte to the second port's device (the mouse), its replies come as
 * its interrupts */
int x86_i8042_aux_write(uint8_t byte)
{
    if (write_wait())
        return -1;

    io_out8(&i8042_ioaddr, COMMAND_PORT, 0xD4);

    if (write_wait())
        return -1;

    io_out8(&i8042_ioaddr, DATA_PORT, byte);
    return 0;
}

void x86_i8042_reboot(void)
{
    io_out8(&i8042_ioaddr, COMMAND_PORT, 0xFE);
}

int x86_i8042_setup(struct ioaddr *io)
{
    i8042_ioaddr = *io;

    int check = kargs_get("i8042.nocheck", NULL);

    if (check && !(read_status() & STATUS_SYSTEM)) {
        printk("i8042: controller not found\n");
        return -1;
    } else {
        printk("i8042: skipping check\n");
    }


    printk("i8042: initializing controller [%p (%s)]\n", io->addr, ioaddr_type_str(io));
    x86_i8042_handler_install();
    return 0;
}
