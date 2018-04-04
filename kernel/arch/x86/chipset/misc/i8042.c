#include <core/system.h>
#include <cpu/cpu.h>
#include <cpu/io.h>
#include <chipset/misc.h>

static struct ioaddr __i8042_ioaddr;

static void (*channel1_handler)(int) = NULL;
static void (*channel2_handler)(int) = NULL;

#define FIRST_IRQ   1
#define SECOND_IRQ  12

#define DATA_PORT   0x00
#define STAT_PORT   0x04

#define __BUF_FULL  0x02

static void x86_i8042_read_wait()
{
    while (io_in8(&__i8042_ioaddr, STAT_PORT) & __BUF_FULL);
}

static void x86_i8042_first_handler()
{
    x86_i8042_read_wait();
    int scancode = io_in8(&__i8042_ioaddr, DATA_PORT);
    if (channel1_handler)
        channel1_handler(scancode);
}

static void x86_i8042_second_handler()
{
    x86_i8042_read_wait();
    int scancode = io_in8(&__i8042_ioaddr, DATA_PORT);
    if (channel2_handler)
        channel2_handler(scancode); 
}

static void x86_i8042_handler_install()
{
    printk("i8042: Installing IRQ %d\n", FIRST_IRQ);
    x86_irq_handler_install(FIRST_IRQ, x86_i8042_first_handler);

    printk("i8042: Installing IRQ %d\n", SECOND_IRQ);
    x86_irq_handler_install(SECOND_IRQ, x86_i8042_second_handler);
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
    __i8042_ioaddr = *io;

    if (!(io_in8(&__i8042_ioaddr, STAT_PORT) & I8042_SYSTEM_FLAG)) {
        printk("i8042: Controller not found");
        return -1;
    }

    printk("i8042: Initalizing controller [%p (%s)]\n", io->addr, ioaddr_type_str(io));
    x86_i8042_handler_install();
    return 0;
}
