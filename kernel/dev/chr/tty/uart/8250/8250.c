#include <core/system.h>
#include <dev/tty.h>
#include <dev/uart.h>
#include <dev/pci.h>
#include <cpu/cpu.h>
#include <cpu/io.h>

struct __uart uart_8250;

#define UART_MMIO   1

//#define COM1 0x3F8
#define COM1 0xCF00B000
#define UART_8250_IRQ   4

static inline int __inb(uintptr_t port, int offset)
{
#if UART_MMIO
    return *(volatile uint32_t *) (port + (offset << 2));
#else
    return inb((uint16_t) (port + offset));
#endif
}

static inline void __outb(uintptr_t port, int offset, int v)
{
#if UART_MMIO
    *(volatile uint32_t *) (port + (offset << 2)) = v;
#else
    outb((uint16_t) (port + offset), (uint8_t) v);
#endif
}

static void init_com1()
{
    __outb(COM1, 3, 0x03);  // 8 bits, no parity, one stop bit
    __outb(COM1, 1, 0x00);  // Disable all interrupts
    __outb(COM1, 2, 0x00);  // Disable FIFO
    __outb(COM1, 4, 0x03);  // DTR + RTS

    __outb(COM1, 3, 0x80);  // Enable DLAB
    __outb(COM1, 0, 0x03);  // Set divisor to 3
    __outb(COM1, 1, 0x00);

	__outb(COM1, 1, 0x01);	// Enable Data/Empty interrupt
}

#if 0
static void init_com1()
{
	outb(COM1 + 1, 0x00);	// Disable all interrupts
	outb(COM1 + 3, 0x80);	// Enable DLAB
	outb(COM1 + 0, 0x03);	// Set divisor to 3
	outb(COM1 + 1, 0x00);
	outb(COM1 + 3, 0x03);	// 8 bits, no parity, one stop bit
	//outb(COM1 + 2, 0xC7);	// Enable FIFO, clear it with 14-byte threshold
	outb(COM1 + 2, 0x00);	// Enable FIFO, clear it with 14-byte threshold
	outb(COM1 + 4, 0x0B);	// IRQs enabled
	outb(COM1 + 1, 0x01);	// Enable Data/Empty interrupt
}
#endif

static int serial_empty()
{
	return __inb(COM1, 5) & 0x20;
}

static int serial_received()
{
   return __inb(COM1, 5) & 0x01;
}

char uart_8250_receive(struct __uart *u __unused)
{
    return __inb(COM1, 0);
}

ssize_t uart_8250_transmit(struct __uart *u __unused, char c)
{
    __outb(COM1, 0, c);
    return 1;
}

void uart_8250_irq()
{
    if (serial_received()) {
        if (uart_8250.inode) /* If open */
            uart_recieve_handler(&uart_8250, 1);
    }

    if (serial_empty()) {
        if (uart_8250.inode) /* If open */
            uart_transmit_handler(&uart_8250, 1);
    }
}

int uart_8250_init()
{
    //init_com1();
    irq_install_handler(UART_8250_IRQ, uart_8250_irq);
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

    uart_register(0, &uart_8250);
    return 0;
}

struct __uart uart_8250 = {
    .transmit = uart_8250_transmit,
    .receive  = uart_8250_receive,
};

MODULE_INIT(uart_8250, uart_8250_init, NULL)
