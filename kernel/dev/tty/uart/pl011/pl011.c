/** \file
 * The driver for ARM's PrimeCell UART (PL011), the serial port of QEMU's
 * virt machine: `/dev/ttyS0`, and `/dev/console` when there are no
 * virtual terminals.
 *
 * Output is written one character at a time, waiting while the transmit
 * FIFO is full. Input comes with the UART's interrupt, which drains the
 * receive FIFO into the terminal. The UART's address and interrupt are
 * the platform's (`dev/platform.h`); the driver drives one UART, the
 * first the platform has.
 *
 * References:
 *      [1] - ARM PrimeCell UART (PL011) Technical Reference Manual (DDI
 *            0183): UARTDR, UARTFR, UARTIMSC, UARTICR
 */

#include <core/system.h>
#include <core/module.h>
#include <cpu/io.h>
#include <dev/tty.h>
#include <dev/uart.h>
#include <dev/irq.h>
#include <dev/platform.h>

#define UART_DR         0x00    /* data */
#define UART_FR         0x18    /* flags */
#define UART_LCR_H      0x2C    /* line control */
#define UART_IMSC       0x38    /* interrupt mask: a bit set enables it */
#define UART_ICR        0x44    /* interrupt clear */

#define FR_RXFE         (1 << 4)    /* receive FIFO empty */
#define FR_TXFF         (1 << 5)    /* transmit FIFO full */
#define INT_RX          (1 << 4)    /* data received */
#define INT_RT          (1 << 6)    /* data received, then a pause */
#define LCR_H_FEN       (1 << 4)    /* the FIFOs enabled */
#define LCR_H_WLEN8     (3 << 5)    /* 8-bit characters */

static struct ioaddr io;

struct uart uart_pl011;

/** Sends one character, once the transmit FIFO has room. */
static ssize_t pl011_transmit(struct uart *u __unused, char c)
{
    /* wait a bounded time: a UART that never takes the character
     * mustn't hang the kernel */
    for (size_t i = 0; i < 100000 && (io_in32(&io, UART_FR) & FR_TXFF); ++i)
        ;

    io_out32(&io, UART_DR, (uint8_t) c);
    return 1;
}

/** Takes one received character from the receive FIFO. */
static char pl011_receive(struct uart *u __unused)
{
    return (char) io_in32(&io, UART_DR);
}

/** Handles the UART's interrupt: every received character goes to the
 * terminal, or is dropped if no one has opened it. */
static void pl011_irq(void *arg __unused)
{
    /* clear the interrupt first, then take every byte in the FIFO: a byte
     * that comes meanwhile raises it again. Cleared after, that byte's
     * interrupt would be lost, and the input with it, since the UART
     * waits for the byte to be read before raising another. */
    io_out32(&io, UART_ICR, INT_RX | INT_RT);

    while (!(io_in32(&io, UART_FR) & FR_RXFE)) {
        if (uart_pl011.tty)     /* if it is open */
            uart_receive_handler(&uart_pl011, 1);
        else
            pl011_receive(&uart_pl011);
    }
}

/** Sets the UART up when it is first opened: 8-bit characters, the
 * FIFOs, and the receive interrupts. QEMU's UART needs no speed set. */
static void pl011_comm_init(struct uart *u __unused)
{
    io_out32(&io, UART_LCR_H, LCR_H_WLEN8 | LCR_H_FEN);
    io_out32(&io, UART_IMSC, INT_RX | INT_RT);
}

/** Finds the platform's PL011, installs its interrupt handler, and
 * registers it as the first UART. */
static int pl011_init(void)
{
    const struct platform_device *pd = platform_device_get("pl011", 0);

    if (!pd || !pd->nirq)
        return -ENODEV;

    int err = platform_device_io(pd, &io);

    if (err || (err = irq_install(pd->irq[0], pl011_irq, NULL)))
        return err;

    uart_register(0, &uart_pl011);
    return 0;
}

struct uart uart_pl011 = {
    .name     = "pl011",
    .init     = pl011_comm_init,
    .transmit = pl011_transmit,
    .receive  = pl011_receive,
};

MODULE_INIT(uart_pl011, pl011_init, NULL)
