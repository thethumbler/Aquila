/** \file
 * ARM's PrimeCell UART (PL011): /dev/ttyS0 (and /dev/console without
 * virtual terminals). Output is written a character at a time, as the
 * transmit FIFO has room; input comes with its interrupt.
 *
 * References:
 *      [1] - ARM PrimeCell UART (PL011) Technical Reference Manual (DDI
 *            0183): UARTDR, UARTFR, UARTIMSC, UARTICR
 *
 * Its address and interrupt are the platform's (dev/platform.h); one
 * UART, the first the platform has.
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
#define UART_IMSC       0x38    /* interrupts' mask: set, enabled */
#define UART_ICR        0x44    /* interrupts cleared */

#define FR_RXFE         (1 << 4)    /* receive FIFO empty */
#define FR_TXFF         (1 << 5)    /* transmit FIFO full */
#define INT_RX          (1 << 4)    /* received */
#define INT_RT          (1 << 6)    /* received, then a pause */
#define LCR_H_FEN       (1 << 4)    /* the FIFOs on */
#define LCR_H_WLEN8     (3 << 5)    /* 8-bit characters */

static struct ioaddr io;

struct uart uart_pl011;

static ssize_t pl011_transmit(struct uart *u __unused, char c)
{
    /* bounded: a UART that never takes it doesn't hang the kernel */
    for (size_t i = 0; i < 100000 && (io_in32(&io, UART_FR) & FR_TXFF); ++i)
        ;

    io_out32(&io, UART_DR, (uint8_t) c);
    return 1;
}

static char pl011_receive(struct uart *u __unused)
{
    return (char) io_in32(&io, UART_DR);
}

static void pl011_irq(void *arg __unused)
{
    /* cleared first, then every byte the FIFO has: a byte that comes
     * meanwhile raises it again (cleared after, its interrupt would be
     * lost, and with it the input: the UART waits for it to be read) */
    io_out32(&io, UART_ICR, INT_RX | INT_RT);

    while (!(io_in32(&io, UART_FR) & FR_RXFE)) {
        if (uart_pl011.tty)     /* if open */
            uart_receive_handler(&uart_pl011, 1);
        else
            pl011_receive(&uart_pl011);
    }
}

/* QEMU's needs no speed set: 8-bit characters, the FIFOs, input's
 * interrupts */
static void pl011_comm_init(struct uart *u __unused)
{
    io_out32(&io, UART_LCR_H, LCR_H_WLEN8 | LCR_H_FEN);
    io_out32(&io, UART_IMSC, INT_RX | INT_RT);
}

static int pl011_init(void)
{
    const struct platform_device *pd = platform_device_get("pl011", 0);

    if (!pd || !pd->nirq || platform_device_io(pd, &io))
        return -1;

    if (irq_install(pd->irq[0], pl011_irq, NULL))
        return -1;

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
