/** \file
 * x86: the early console on the first serial port (COM1, port 0x3F8),
 * polled: each character waits until the UART can take it.
 */

#include <core/system.h>
#include <core/string.h>
#include <console/earlycon.h>
#include <cpu/io.h>
#include <mm/mm.h>

#define SERIAL_MMIO 0

#if SERIAL_MMIO
static struct ioaddr earlycon_uart_ioaddr = {
    .addr = 0xCF00B000,
    .type = IOADDR_MMIO32,
};
#else
static struct ioaddr earlycon_uart_ioaddr = {
    .addr = 0x3F8,
    .type = IOADDR_PORT,
};
#endif

#define UART_IER    1
#define UART_FCR    2
#define UART_LCR    3
#define UART_MCR    4
#define UART_DLL    0
#define UART_DLH    1
#define UART_LCR_DLAB    0x80

/** Sets the UART up: no interrupts, no FIFO, 38400 baud, 8 bits, no
 * parity, one stop bit. */
static void serial_init(void)
{
    io_out8(&earlycon_uart_ioaddr, UART_IER, 0x00);    /* no interrupts */
    io_out8(&earlycon_uart_ioaddr, UART_FCR, 0x00);    /* no FIFO */
    io_out8(&earlycon_uart_ioaddr, UART_LCR, 0x03);    /* 8 bits, no parity, one stop bit */
    io_out8(&earlycon_uart_ioaddr, UART_MCR, 0x03);    /* RTS and DTR */

    uint8_t lcr = io_in8(&earlycon_uart_ioaddr, UART_LCR);
    io_out8(&earlycon_uart_ioaddr, UART_LCR, lcr | UART_LCR_DLAB);  /* the divisor's registers */
    io_out8(&earlycon_uart_ioaddr, UART_DLL, 0x03);    /* divisor 3: 38400 baud (low byte) */
    io_out8(&earlycon_uart_ioaddr, UART_DLH, 0x00);    /* (high byte) */
    io_out8(&earlycon_uart_ioaddr, UART_LCR, lcr & ~UART_LCR_DLAB); /* back to the data registers */
}

/** Returns whether the UART's transmit holding register is empty. */
static int serial_tx_empty(void)
{
    return io_in8(&earlycon_uart_ioaddr, 5) & 0x20;
}

/** Writes a character, a newline as CR LF. Returns 1. */
static int serial_chr(char chr)
{
    if (chr == '\n')
        serial_chr('\r');

    while (!serial_tx_empty());

    io_out8(&earlycon_uart_ioaddr, 0, chr);

    return 1;
}

/** Writes a string, and returns a count of what it wrote. */
static int serial_str(char *str)
{
    int ret = 0;

    while (*str) {
        ++ret;
        ret += serial_chr(*str++);
    }

    return ret;
}

static int earlycon_uart_puts(char *s)
{
    return serial_str(s);
}

static int earlycon_uart_putc(char c)
{
    return serial_chr(c);
}

static void earlycon_uart_init(void)
{
#if SERIAL_MMIO
    pmman.map_to(0x9000B000, 0xCF00B000, PAGE_SIZE, VM_KRW);
#endif
    serial_init();

    /* there's probably a terminal at the other end: reset its attributes */
    serial_str("\033[0m");
}

struct earlycon earlycon_uart = {
    .init = earlycon_uart_init,
    .putc = earlycon_uart_putc,
    .puts = earlycon_uart_puts,
};
