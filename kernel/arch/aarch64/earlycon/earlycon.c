/** \file
 * The early console: the PL011 UART, written to a character at a time
 * (polling), until the terminals are up
 */

#include <core/system.h>
#include <console/earlycon.h>
#include <cpu/io.h>
#include <platform/virt.h>

#define UART_DR     0x00    /* data */
#define UART_FR     0x18    /* flags */
#define UART_FR_TXFF  (1 << 5)  /* the transmit FIFO is full */

static uintptr_t uart = VIRT_REG(VIRT_UART);

int earlycon_putc(char c)
{
    if (c == '\n')
        earlycon_putc('\r');

    while (mmio_read32(uart + UART_FR) & UART_FR_TXFF)
        ;

    mmio_write32(uart + UART_DR, (uint8_t) c);
    return 1;
}

int earlycon_puts(char *s)
{
    int n = 0;

    while (*s)
        n += earlycon_putc(*s++);

    return n;
}

int earlycon_on_screen(void)
{
    return 0;
}

/* QEMU's PL011 needs no setup: it's on, at any speed */
void earlycon_init(void)
{
}

void earlycon_reinit(void)
{
}
