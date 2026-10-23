/** \file
 * The i8042, the PC's keyboard controller: its two PS/2 ports, the
 * keyboard's (the first) and the mouse's (the second, "aux"), each with
 * its interrupt. Their bytes go to the handler their driver registered
 * (dev/kbd/ps2kbd.c, dev/mouse/ps2mouse.c). Where it is, the platform
 * says (dev/platform.h: "i8042", its two interrupts in the ports' order).
 */

#include <core/system.h>
#include <core/string.h>
#include <core/kargs.h>
#include <core/module.h>
#include <cpu/io.h>
#include <dev/irq.h>
#include <dev/platform.h>
#include <dev/i8042.h>

static struct ioaddr i8042_ioaddr;
static int i8042_found;

static void (*port_handler[2])(int);

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

/** the controller has a byte for us (its output buffer is full): the
 * interrupt comes when it has, there is nothing to wait for; one with
 * nothing to read is spurious */
static int has_byte(void)
{
    return read_status() & STATUS_OBUF_FULL;
}

/** a port's interrupt (`arg' is its handler's slot) */
static void i8042_irq(void *arg)
{
    void (**handler)(int) = arg;

    if (!has_byte())
        return;

    int byte = io_in8(&i8042_ioaddr, DATA_PORT);

    if (*handler)
        (*handler)(byte);
}

void i8042_handler_register(int port, void (*fun)(int))
{
    if (port == 1 || port == 2)
        port_handler[port - 1] = fun;
}

/** the controller takes a byte, -1 if it doesn't in time */
static int write_wait(void)
{
    for (int i = 0; i < 100000; ++i)
        if (!(read_status() & STATUS_IBUF_FULL))
            return 0;

    return -1;
}

/** a byte to the second port's device (the mouse), its replies come as
 * its interrupts */
int i8042_aux_write(uint8_t byte)
{
    if (!i8042_found || write_wait())
        return -1;

    io_out8(&i8042_ioaddr, COMMAND_PORT, 0xD4);

    if (write_wait())
        return -1;

    io_out8(&i8042_ioaddr, DATA_PORT, byte);
    return 0;
}

/** the CPU reset, through the controller's output line */
void i8042_reboot(void)
{
    if (i8042_found)
        io_out8(&i8042_ioaddr, COMMAND_PORT, 0xFE);
}

/** the controller's reply to a command, -1 if none comes in time */
static int read_wait(void)
{
    for (int i = 0; i < 100000; ++i)
        if (has_byte())
            return io_in8(&i8042_ioaddr, DATA_PORT);

    return -1;
}

#define CMD_READ_CONFIG   0x20
#define CMD_WRITE_CONFIG  0x60
#define CMD_AUX_ENABLE    0xA8
#define CONFIG_AUX_IRQ    0x02

/** the second port on, and its interrupt (the configuration byte's);
 * polled, before the interrupts are installed */
static int aux_enable(void)
{
    if (write_wait())
        return -1;

    io_out8(&i8042_ioaddr, COMMAND_PORT, CMD_AUX_ENABLE);

    if (write_wait())
        return -1;

    io_out8(&i8042_ioaddr, COMMAND_PORT, CMD_READ_CONFIG);
    int config = read_wait();

    if (config < 0 || write_wait())
        return -1;

    io_out8(&i8042_ioaddr, COMMAND_PORT, CMD_WRITE_CONFIG);

    if (write_wait())
        return -1;

    io_out8(&i8042_ioaddr, DATA_PORT, config | CONFIG_AUX_IRQ);
    return 0;
}

static int i8042_init(void)
{
    const struct platform_device *pd = platform_device_get("i8042", 0);

    if (!pd || platform_device_io(pd, &i8042_ioaddr))
        return -1;

    /* i8042.nocheck: there even if its status says no */
    if (kargs_get("i8042.nocheck", NULL)) {
        if (!(read_status() & STATUS_SYSTEM)) {
            printk("i8042: controller not found\n");
            return -1;
        }
    } else {
        printk("i8042: skipping check\n");
    }

    i8042_found = 1;
    printk("i8042: controller at %p (%s)\n", (void *) pd->io.addr, ioaddr_type_str(&i8042_ioaddr));

    /* the mouse's port, if it has its interrupt */
    if (pd->nirq > 1 && aux_enable())
        printk("i8042: the second port didn't come on\n");

    /* flush any pending data */
    for (int i = 0; i < 10; ++i)
        io_in8(&i8042_ioaddr, DATA_PORT);

    for (int i = 0; i < pd->nirq && i < 2; ++i)
        irq_install(pd->irq[i], i8042_irq, &port_handler[i]);

    return 0;
}

MODULE_INIT(i8042, i8042_init, NULL)
