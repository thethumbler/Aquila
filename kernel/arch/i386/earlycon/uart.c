#include <core/system.h>
#include <core/string.h>
#include <console/earlycon.h>
#include <cpu/io.h>
#include <mm/mm.h>

#define SERIAL_MMIO 0

#if SERIAL_MMIO
struct ioaddr earlycon_uart_ioaddr = {
    .addr = 0xCF00B000,
    .type = IOADDR_MMIO32,
};
#else
struct ioaddr earlycon_uart_ioaddr = {
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

static void serial_init(void)
{
    io_out8(&earlycon_uart_ioaddr, UART_IER, 0x00);    /* Disable all interrupts */
    io_out8(&earlycon_uart_ioaddr, UART_FCR, 0x00);    /* Disable FIFO */
    io_out8(&earlycon_uart_ioaddr, UART_LCR, 0x03);    /* 8 bits, no parity, one stop bit */
    io_out8(&earlycon_uart_ioaddr, UART_MCR, 0x03);    /* RTS + DTR */

    uint8_t lcr = io_in8(&earlycon_uart_ioaddr, UART_LCR);
    io_out8(&earlycon_uart_ioaddr, UART_LCR, lcr | UART_LCR_DLAB);  /* Enable DLAB */
    io_out8(&earlycon_uart_ioaddr, UART_DLL, 0x18);    /* Set divisor to 1 (lo byte) 115200 baud */
    io_out8(&earlycon_uart_ioaddr, UART_DLH, 0x00);    /*                  (hi byte)            */
    io_out8(&earlycon_uart_ioaddr, UART_LCR, lcr & ~UART_LCR_DLAB); /* Disable DLAB */
}

static int serial_tx_empty(void)
{
    return io_in8(&earlycon_uart_ioaddr, 5) & 0x20;
}

static int serial_chr(char chr)
{
    if (chr == '\n')
        serial_chr('\r');

    while (!serial_tx_empty());

    io_out8(&earlycon_uart_ioaddr, 0, chr);

    return 1;
}

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

    /* Assume a terminal, clear formatting */
    serial_str("\033[0m");
}

struct earlycon earlycon_uart = {
    .init = earlycon_uart_init,
    .putc = earlycon_uart_putc,
    .puts = earlycon_uart_puts,
};
