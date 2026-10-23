/** \file
 * ARM's PrimeCell UART (PL011): /dev/ttyS0 (and /dev/console without
 * virtual terminals). Output is written a character at a time, as the
 * transmit FIFO has room; input comes with its interrupt.
 *
 * References:
 *      [1] - ARM PrimeCell UART (PL011) Technical Reference Manual (DDI
 *            0183): UARTDR, UARTFR, UARTIMSC, UARTICR
 *
 * Its address and interrupt are the platform's (QEMU's virt machine):
 * the driver reaches them through <platform/virt.h>, as x86's drivers do
 * x86's (architecture.md, "Where the line is blurred").
 */

#include <core/system.h>
#include <core/module.h>
#include <cpu/io.h>
#include <dev/tty.h>
#include <dev/uart.h>
#include <platform/virt.h>

#define UART_DR         0x00    /* data */
#define UART_FR         0x18    /* flags */
#define UART_IMSC       0x38    /* interrupts' mask: set, enabled */
#define UART_ICR        0x44    /* interrupts cleared */

#define FR_RXFE         (1 << 4)    /* receive FIFO empty */
#define FR_TXFF         (1 << 5)    /* transmit FIFO full */
#define INT_RX          (1 << 4)    /* received */
#define INT_RT          (1 << 6)    /* received, then a pause */

static uintptr_t base = VIRT_REG(VIRT_UART);

struct uart uart_pl011;

static ssize_t pl011_transmit(struct uart *u __unused, char c)
{
    /* bounded: a UART that never takes it doesn't hang the kernel */
    for (size_t i = 0; i < 100000 && (mmio_read32(base + UART_FR) & FR_TXFF); ++i)
        ;

    mmio_write32(base + UART_DR, (uint8_t) c);
    return 1;
}

static char pl011_receive(struct uart *u __unused)
{
    return (char) mmio_read32(base + UART_DR);
}

static void pl011_irq(void)
{
    /* every byte the FIFO has */
    while (!(mmio_read32(base + UART_FR) & FR_RXFE)) {
        if (uart_pl011.tty)     /* if open */
            uart_receive_handler(&uart_pl011, 1);
        else
            pl011_receive(&uart_pl011);
    }

    mmio_write32(base + UART_ICR, INT_RX | INT_RT);
}

/* QEMU's needs no speed set: input's interrupts only */
static void pl011_comm_init(struct uart *u __unused)
{
    mmio_write32(base + UART_IMSC, INT_RX | INT_RT);
}

static int pl011_init(void)
{
    irq_install(VIRT_UART_IRQ, pl011_irq);
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
