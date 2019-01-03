#include <core/system.h>
#include <core/string.h>
#include <core/kargs.h>
#include <cpu/cpu.h>
#include <cpu/io.h>
#include <platform/misc.h>

static struct ioaddr i8042_ioaddr;

static void (*channel1_handler)(int) = NULL;
static void (*channel2_handler)(int) = NULL;

#define FIRST_IRQ   1
#define SECOND_IRQ  12

#define DATA_PORT       0x00
#define STATUS_PORT     0x04
#define COMMAND_PORT    0x04

#define __BUF_FULL  0x02

static uint8_t read_status(void)
{
    return io_in8(&i8042_ioaddr, STATUS_PORT);
}

static void x86_i8042_read_wait(void)
{
    /* TODO -- timeout */
    while (read_status() & __BUF_FULL);
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
    printk("i8042: Installing IRQ %d\n", FIRST_IRQ);
    x86_irq_handler_install(FIRST_IRQ, x86_i8042_first_handler);

    printk("i8042: Installing IRQ %d\n", SECOND_IRQ);
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

#define I8042_SYSTEM_FLAG   0x4

int x86_i8042_setup(struct ioaddr *io)
{
    i8042_ioaddr = *io;

    int check = kargs_get("i8042.nocheck", NULL);

    if (check && !(read_status() & I8042_SYSTEM_FLAG)) {
        printk("i8042: Controller not found\n");
        return -1;
    } else {
        printk("i8042: Skipping check\n");
    }

    printk("i8042: Initializing controller [%p (%s)]\n", io->addr, ioaddr_type_str(io));
    x86_i8042_handler_install();
    return 0;
}
