/** \file
 * The driver for the i8042, the PC's keyboard controller.
 *
 * The controller has two PS/2 ports, the keyboard's (the first) and the
 * mouse's (the second, "aux"), each with its own interrupt. The bytes
 * each port receives go to the handler that the port's driver registered
 * (`dev/kbd/ps2kbd.c`, `dev/mouse/ps2mouse.c`). The controller can also
 * reset the CPU, which is how a PC reboots. The platform says where it is
 * (`dev/platform.h`: `"i8042"`, with its two interrupts in the ports'
 * order).
 */

#include <core/system.h>
#include <core/string.h>
#include <core/kargs.h>
#include <core/module.h>
#include <cpu/io.h>
#include <dev/irq.h>
#include <dev/platform.h>
#include <dev/i8042.h>
#include <aquila/errno.h>

static struct ioaddr i8042_ioaddr;
static int i8042_found;

/* the handler of each port's bytes */
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

/** Returns the controller's status register. */
static uint8_t read_status(void)
{
    return io_in8(&i8042_ioaddr, STATUS_PORT);
}

/** Returns whether the controller has a byte for us (its output buffer is
 * full). The interrupt only comes once it has one, so there is nothing to
 * wait for; an interrupt with nothing to read is spurious. */
static int has_byte(void)
{
    return read_status() & STATUS_OBUF_FULL;
}

/** Handles a port's interrupt: its byte goes to the port's handler.
 * `arg` is the port's slot in `port_handler`. */
static void i8042_irq(void *arg)
{
    void (**handler)(int) = arg;

    if (!has_byte())
        return;

    int byte = io_in8(&i8042_ioaddr, DATA_PORT);

    if (*handler)
        (*handler)(byte);
}

/**
 * Registers the function that gets each byte a port receives.
 *
 * @param port  the port: 1 (the keyboard) or 2 (the mouse)
 * @param fun   the function, called with each byte, from the interrupt
 */
void i8042_handler_register(int port, void (*fun)(int))
{
    if (port == 1 || port == 2)
        port_handler[port - 1] = fun;
}

/** Waits until the controller can take a byte. Returns 0, or
 * `-ETIMEDOUT` if it doesn't in time. */
static int write_wait(void)
{
    for (int i = 0; i < 100000; ++i)
        if (!(read_status() & STATUS_IBUF_FULL))
            return 0;

    return -ETIMEDOUT;
}

/**
 * Sends a byte to the device on the second port (the mouse). Its replies
 * come as that port's interrupts.
 *
 * @param byte  the byte
 * @return 0, `-ENODEV` if there is no controller, or `-ETIMEDOUT` if it
 *         didn't take the byte in time
 */
int i8042_aux_write(uint8_t byte)
{
    if (!i8042_found)
        return -ENODEV;

    int err = write_wait();

    if (err)
        return err;

    io_out8(&i8042_ioaddr, COMMAND_PORT, 0xD4);

    if ((err = write_wait()))
        return err;

    io_out8(&i8042_ioaddr, DATA_PORT, byte);
    return 0;
}

/**
 * Resets the CPU, through the controller's output line: this reboots the
 * PC. Without a controller, it does nothing.
 */
void i8042_reboot(void)
{
    if (i8042_found)
        io_out8(&i8042_ioaddr, COMMAND_PORT, 0xFE);
}

/** Waits for the controller's reply to a command. Returns the reply, or
 * `-ETIMEDOUT` if none comes in time. */
static int read_wait(void)
{
    for (int i = 0; i < 100000; ++i)
        if (has_byte())
            return io_in8(&i8042_ioaddr, DATA_PORT);

    return -ETIMEDOUT;
}

#define CMD_READ_CONFIG   0x20
#define CMD_WRITE_CONFIG  0x60
#define CMD_AUX_ENABLE    0xA8
#define CONFIG_AUX_IRQ    0x02

/** Turns the second port on, and its interrupt (a bit of the
 * controller's configuration byte). It polls: the interrupt handlers
 * aren't installed yet. Returns 0 or a negative errno. */
static int aux_enable(void)
{
    int err, config;

    if ((err = write_wait()))
        return err;

    io_out8(&i8042_ioaddr, COMMAND_PORT, CMD_AUX_ENABLE);

    if ((err = write_wait()))
        return err;

    io_out8(&i8042_ioaddr, COMMAND_PORT, CMD_READ_CONFIG);

    if ((config = read_wait()) < 0)
        return config;

    if ((err = write_wait()))
        return err;

    io_out8(&i8042_ioaddr, COMMAND_PORT, CMD_WRITE_CONFIG);

    if ((err = write_wait()))
        return err;

    io_out8(&i8042_ioaddr, DATA_PORT, config | CONFIG_AUX_IRQ);
    return 0;
}

/** Finds the platform's i8042, turns the mouse's port on, and installs
 * the ports' interrupt handlers. */
static int i8042_init(void)
{
    const struct platform_device *pd = platform_device_get("i8042", 0);

    if (!pd)
        return -ENODEV;

    int err = platform_device_io(pd, &i8042_ioaddr);

    if (err)
        return err;

    /* the controller should say it passed its self-test; with the
     * i8042.nocheck argument, that isn't checked */
    if (kargs_get("i8042.nocheck", NULL)) {
        if (!(read_status() & STATUS_SYSTEM)) {
            printk("i8042: controller not found\n");
            return -ENODEV;
        }
    } else {
        printk("i8042: skipping check\n");
    }

    i8042_found = 1;
    printk("i8042: controller at %p (%s)\n", (void *) pd->io.addr, ioaddr_type_str(&i8042_ioaddr));

    /* the mouse's port, if the platform gives it an interrupt */
    if (pd->nirq > 1 && aux_enable())
        printk("i8042: the second port didn't come on\n");

    /* drop any bytes waiting from before */
    for (int i = 0; i < 10; ++i)
        io_in8(&i8042_ioaddr, DATA_PORT);

    for (int i = 0; i < pd->nirq && i < 2; ++i)
        irq_install(pd->irq[i], i8042_irq, &port_handler[i]);

    return 0;
}

MODULE_INIT(i8042, i8042_init, NULL)
