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

static void x86_i8042_read_wait(void)
{
    /* TODO -- timeout */
    while (read_status() & STATUS_IBUF_FULL);
}

static int x86_i8042_check(void)
{
    io_out8(&i8042_ioaddr, COMMAND_PORT, 0xAA);
    while (!(read_status() & STATUS_OBUF_FULL));
    uint8_t r = io_in8(&i8042_ioaddr, DATA_PORT);
    return r == 0x55;
}

static void x86_i8042_first_handler(struct x86_regs *r)
{
    (void) r;

    x86_i8042_read_wait();
    int scancode = io_in8(&i8042_ioaddr, DATA_PORT);

    if (channel1_handler)
        channel1_handler(scancode);
}

static void x86_i8042_second_handler(struct x86_regs *r)
{
    (void) r;

    x86_i8042_read_wait();
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

    //x86_i8042_check();

    printk("i8042: initializing controller [%p (%s)]\n", io->addr, ioaddr_type_str(io));
    x86_i8042_handler_install();
    return 0;
}
