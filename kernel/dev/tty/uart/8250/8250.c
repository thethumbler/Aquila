/** \file
 * The driver for the 8250 UART and its successors, such as the 16550 with
 * its FIFOs: the PC's serial ports, `/dev/ttyS0` and up.
 *
 * The driver attaches to each 8250 the platform lists (`dev/platform.h`)
 * that is really there, in the platform's order. Output is written one
 * character at a time, waiting until the transmitter is empty. Input comes
 * with the UART's interrupt, which drains the receive FIFO into the
 * terminal. The registers may be ports or memory (`struct ioaddr`).
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

#define UART_IER    1   /* interrupt enable */
#define UART_FCR    2   /* FIFO control */
#define UART_LCR    3   /* line control */
#define UART_MCR    4   /* modem control */
#define UART_LSR    5   /* line status */
#define UART_SCR    7   /* scratch */
#define UART_DLL    0   /* divisor latch, low byte (when DLAB is set) */
#define UART_DLH    1   /* divisor latch, high byte (when DLAB is set) */
#define UART_LCR_DLAB    0x80

#define UART_8250_MAX   4

/** One UART: the terminal layer's `struct uart` (first, so that a
 * pointer to one is a pointer to the other), and its registers. */
struct uart_8250 {
    struct uart uart;
    struct ioaddr io;
};

/* the registers of the UART `u` (a struct uart *) */
#define IO(u)   (&((struct uart_8250 *) (u))->io)

/** Returns whether the transmitter can take a character. */
static int serial_empty(struct ioaddr *io)
{
    return io_in8(io, UART_LSR) & 0x20;
}

/** Returns whether a received character is waiting. */
static int serial_received(struct ioaddr *io)
{
   return io_in8(io, UART_LSR) & 0x01;
}

/** Takes one received character. */
static char uart_8250_receive(struct uart *u)
{
    return io_in8(IO(u), 0);
}

/** Sends one character, once the transmitter can take it. */
static ssize_t uart_8250_transmit(struct uart *u, char c)
{
    /* wait, a bounded time, for the transmit holding register to empty */
    for (size_t i = 0; i < 100000 && !serial_empty(IO(u)); ++i)
        ;

    io_out8(IO(u), 0, c);
    return 1;
}

/** Handles a UART's interrupt: every received character goes to the
 * terminal, or is dropped if no one has opened it. */
static void uart_8250_irq(void *arg)
{
    struct uart *u = arg;

    /* take every byte the FIFO has: one interrupt may come for several
     * bytes, and no other may come for those left (v86's 16550 doesn't
     * raise another) */
    while (serial_received(IO(u))) {
        if (u->tty)   /* if it is open */
            uart_receive_handler(u, 1);
        else
            uart_8250_receive(u);
    }

    if (serial_empty(IO(u))) {
        if (u->tty)   /* if it is open */
            uart_transmit_handler(u, 1);
    }
}

/** Sets a UART up when it is first opened: 38400 baud, 8 bits, no
 * parity, one stop bit, the FIFOs, and the receive interrupt. */
static void uart_8250_comm_init(struct uart *u)
{
    struct ioaddr *io = IO(u);

    /* let the output still in the UART go first; the wait is bounded, as
     * in uart_8250_transmit(), for a UART that never empties */
    for (size_t i = 0; i < 100000 && !serial_empty(io); ++i)
        ;

    io_out8(io, UART_IER, 0x00);  /* no interrupts meanwhile */

    io_out8(io, UART_LCR, 0x03);  /* 8 bits, no parity, one stop bit */
    io_out8(io, UART_FCR, 0xC7);  /* the FIFOs on and cleared, 14-byte threshold */
    io_out8(io, UART_MCR, 0x0B);  /* DTR, RTS, and OUT2 (interrupts) */

    /* the speed: the divisor of 115200, written with DLAB set */
    int lcr = io_in8(io, UART_LCR);
    io_out8(io, UART_LCR, lcr | UART_LCR_DLAB);
    io_out8(io, UART_DLL, 0x03);  /* 115200 / 3: 38400 baud */
    io_out8(io, UART_DLH, 0x00);
    io_out8(io, UART_LCR, lcr & ~UART_LCR_DLAB);

    io_out8(io, UART_IER, 0x01);  /* the "data received" interrupt only */
}

/** Returns whether a UART is really there: its scratch register keeps
 * what is written to it, where no device would read back all ones. */
static int uart_8250_present(struct ioaddr *io)
{
    io_out8(io, UART_SCR, 0x5A);

    if (io_in8(io, UART_SCR) != 0x5A)
        return 0;

    io_out8(io, UART_SCR, 0xA5);
    return io_in8(io, UART_SCR) == 0xA5;
}

/** Attaches to one of the platform's 8250s, if it is there, as
 * `/dev/ttyS<id>`. Returns 0, or a negative errno. */
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

/** Attaches to the platform's 8250s that are there, as `/dev/ttyS0` and
 * up, in the platform's order. */
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
