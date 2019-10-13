#include <core/system.h>
#include <core/module.h>

#include <dev/tty.h>
#include <dev/uart.h>
#include <dev/pci.h>
#include <cpu/cpu.h>
#include <cpu/io.h>

#include <platform/misc.h>   /* XXX */

#define UART_MMIO   0

#define UART_IER    1
#define UART_FCR    2
#define UART_LCR    3
#define UART_MCR    4
#define UART_DLL    0
#define UART_DLH    1
#define UART_LCR_DLAB    0x80

struct uart uart_8250;
struct ioaddr io8250 = {
#if UART_MMIO
    .addr = 0xCF00B000,
    .type = IOADDR_MMIO32,
#else
    .addr = 0x3F8,
    .type = IOADDR_PORT,
#endif
};

#define UART_8250_IRQ   4


static int serial_empty(void)
{
    return io_in8(&io8250, 5) & 0x20;
}

static int serial_received(void)
{
   return io_in8(&io8250, 5) & 0x01;
}

char uart_8250_receive(struct uart *u __unused)
{
    return io_in8(&io8250, 0);
}

ssize_t uart_8250_transmit(struct uart *u __unused, char c)
{
    io_out8(&io8250, 0, c);
    return 1;
}

void uart_8250_irq()
{
    if (serial_received()) {
        if (uart_8250.vnode) /* If open */
            uart_recieve_handler(&uart_8250, 1);
    }

    if (serial_empty()) {
        if (uart_8250.vnode) /* If open */
            uart_transmit_handler(&uart_8250, 1);
    }
}

void uart_8250_comm_init(struct uart *u __unused)
{
    while (!serial_empty());    /* Flush all output before reseting */

    io_out8(&io8250, UART_IER, 0x00);  /* Disable all interrupts */

    io_out8(&io8250, UART_LCR, 0x03);  /* 8 bits, no parity, one stop bit */
    io_out8(&io8250, UART_FCR, 0xC7);  /* Enalbe FIFO, clear, 14 byte threshold */
    io_out8(&io8250, UART_MCR, 0x0B);  /* DTR + RTS */

    /* Enable DLAB and set divisor */
    int lcr = io_in8(&io8250, UART_LCR);
    io_out8(&io8250, UART_LCR, lcr | UART_LCR_DLAB);  /* Enable DLAB */
    io_out8(&io8250, UART_DLL, 0x03);  /* Set divisor to 3 */
    io_out8(&io8250, UART_DLH, 0x00);
    io_out8(&io8250, UART_LCR, lcr & ~UART_LCR_DLAB);

    io_out8(&io8250, UART_IER, 0x01);  /* Enable Data/Empty interrupt */
}

int uart_8250_init()
{
    //serial_init();
    x86_irq_handler_install(UART_8250_IRQ, uart_8250_irq);

#if 0
    struct pci_dev dev;
    int ret = pci_device_scan(0x8086, 0x0936, &dev);

    if (ret) {
        printk("HSUART not found\n");
        for (;;);
    }

    printk("HSUART found at bus %d, device %d, function %d\n", dev.bus, dev.dev, dev.func);
    uint16_t cmd = pci_reg8_read(&dev, 0x4);
    printk("cmd %x\n", cmd);

    uint8_t intl = pci_reg8_read(&dev, 0x3C);
    printk("intl %x\n", intl);
    uint8_t intp = pci_reg8_read(&dev, 0x3D);
    printk("intp %x\n", intp);
    intl = UART_8250_IRQ;
    pci_reg8_write(&dev, 0x3C, intl);
    intl = pci_reg8_read(&dev, 0x3C);
    printk("intl %x\n", intl);

    for (;;)
        asm volatile ("sti; hlt;");

    for (;;) {
        asm volatile ("sti; hlt;");

        uintptr_t port = 0xCF00B000;
        uint32_t stat = *(volatile uint32_t *) (port + (5 << 2));

        //uint32_t _stat = *(volatile uint32_t *) (port + (4 << 2));

        //printk("stat = %x\n", _stat);

        if (stat & 0x01) { /* Got data */
            uint32_t data = *(volatile uint32_t *) (port + (0 << 2));
            printk("data = %c\n", data);
        }

        asm volatile ("cli;");
    }
#endif

    uart_register(0, &uart_8250);
    return 0;
}

struct uart uart_8250 = {
    .name     = "8250",
    .init     = uart_8250_comm_init,
    .transmit = uart_8250_transmit,
    .receive  = uart_8250_receive,
};

MODULE_INIT(uart_8250, uart_8250_init, NULL)
