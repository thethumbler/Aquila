/** \file
 * The 8250 UART and its successors (the 16550's FIFOs): /dev/ttyS0 and
 * up, one for each the platform has (dev/platform.h) and is there.
 * Output is written a character at a time, as the transmitter empties;
 * input comes with its interrupt.
 */

#include <core/system.h>
#include <core/module.h>

#include <dev/tty.h>
#include <dev/uart.h>
#include <dev/irq.h>
#include <dev/platform.h>
#include <cpu/io.h>
#include <mm/mm.h>

MALLOC_DEFINE(M_UART_8250, "uart-8250", "8250 UART");

#define UART_IER    1
#define UART_FCR    2
#define UART_LCR    3
#define UART_MCR    4
#define UART_LSR    5
#define UART_SCR    7
#define UART_DLL    0
#define UART_DLH    1
#define UART_LCR_DLAB    0x80

#define UART_8250_MAX   4

/** a UART: its registers (the struct uart first, it's the driver's) */
struct uart_8250 {
    struct uart uart;
    struct ioaddr io;
};

#define IO(u)   (&((struct uart_8250 *) (u))->io)

static int serial_empty(struct ioaddr *io)
{
    return io_in8(io, UART_LSR) & 0x20;
}

static int serial_received(struct ioaddr *io)
{
   return io_in8(io, UART_LSR) & 0x01;
}

static char uart_8250_receive(struct uart *u)
{
    return io_in8(IO(u), 0);
}

static ssize_t uart_8250_transmit(struct uart *u, char c)
{
    /* wait (bounded) for the transmit holding register to be empty */
    for (size_t i = 0; i < 100000 && !serial_empty(IO(u)); ++i)
        ;

    io_out8(IO(u), 0, c);
    return 1;
}

static void uart_8250_irq(void *arg)
{
    struct uart *u = arg;

    /* every byte the FIFO has: one interrupt may come for several, and
     * another may not come for those left (v86's 16550 doesn't) */
    while (serial_received(IO(u))) {
        if (u->tty)   /* If open */
            uart_receive_handler(u, 1);
        else
            uart_8250_receive(u);
    }

    if (serial_empty(IO(u))) {
        if (u->tty)   /* If open */
            uart_transmit_handler(u, 1);
    }
}

static void uart_8250_comm_init(struct uart *u)
{
    struct ioaddr *io = IO(u);

    /* the output still there is sent before the reset; bounded, as in
     * uart_8250_transmit(), for a UART that never empties */
    for (size_t i = 0; i < 100000 && !serial_empty(io); ++i)
        ;

    io_out8(io, UART_IER, 0x00);  /* Disable all interrupts */

    io_out8(io, UART_LCR, 0x03);  /* 8 bits, no parity, one stop bit */
    io_out8(io, UART_FCR, 0xC7);  /* Enalbe FIFO, clear, 14 byte threshold */
    io_out8(io, UART_MCR, 0x0B);  /* DTR + RTS */

    /* Enable DLAB and set divisor */
    int lcr = io_in8(io, UART_LCR);
    io_out8(io, UART_LCR, lcr | UART_LCR_DLAB);  /* Enable DLAB */
    io_out8(io, UART_DLL, 0x03);  /* Set divisor to 3 */
    io_out8(io, UART_DLH, 0x00);
    io_out8(io, UART_LCR, lcr & ~UART_LCR_DLAB);

    io_out8(io, UART_IER, 0x01);  /* Enable Data/Empty interrupt */
}

/** a UART answers there: its scratch register keeps what's written (no
 * device reads all ones) */
static int uart_8250_present(struct ioaddr *io)
{
    io_out8(io, UART_SCR, 0x5A);

    if (io_in8(io, UART_SCR) != 0x5A)
        return 0;

    io_out8(io, UART_SCR, 0xA5);
    return io_in8(io, UART_SCR) == 0xA5;
}

static int uart_8250_attach(const struct platform_device *pd, int id)
{
    struct uart_8250 *u = kmalloc(sizeof(*u), &M_UART_8250, M_ZERO);

    if (!u)
        return -ENOMEM;

    int err = platform_device_io(pd, &u->io);

    if (!err && (!uart_8250_present(&u->io) || !pd->nirq))
        err = -ENODEV;

    if (err) {
        kfree(u);
        return err;
    }

    u->uart.name     = "8250";
    u->uart.init     = uart_8250_comm_init;
    u->uart.transmit = uart_8250_transmit;
    u->uart.receive  = uart_8250_receive;

    if ((err = irq_install(pd->irq[0], uart_8250_irq, u))) {
        kfree(u);
        return err;
    }

    printk("8250: ttyS%d at %p (%s), irq %d\n", id, (void *) pd->io.addr,
           ioaddr_type_str(&u->io), pd->irq[0]);

    uart_register(id, &u->uart);
    return 0;
}

/** the platform's UARTs that are there: ttyS0 and up, in its order */
static int uart_8250_init(void)
{
    const struct platform_device *pd;
    int id = 0;

    for (int i = 0; id < UART_8250_MAX && (pd = platform_device_get("8250", i)); ++i)
        if (!uart_8250_attach(pd, id))
            ++id;

    return 0;
}

MODULE_INIT(uart_8250, uart_8250_init, NULL)
