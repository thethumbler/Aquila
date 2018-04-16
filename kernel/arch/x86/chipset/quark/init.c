#include <core/system.h>
#include <chipset/system.h>
#include <chipset/misc.h>
#include <cpu/io.h>
#include <console/earlycon.h>
#include <dev/pci.h>

#include <chipset/legacy_bridge.h>
#include <chipset/root_complex.h>

#define PIC_MASTER	0x20
#define PIC_SLAVE	0xA0

int quark_pic_setup()
{
    struct ioaddr pic_master;
    struct ioaddr pic_slave;

    pic_master.addr = PIC_MASTER;
    pic_master.type = IOADDR_PORT;
    pic_slave.addr  = PIC_SLAVE;
    pic_slave.type  = IOADDR_PORT;

    return x86_pic_setup(&pic_master, &pic_slave);
}

void quark_pci_init()
{
    printk("Quark SoC: Initializing PCI\n");
    struct ioaddr pci;
    pci.addr = 0xCF8;
    pci.type = IOADDR_PORT;
    pci_ioaddr_set(&pci);
}

#define QRK_HSUART_VENDOR_ID  0x8086
#define QRK_HSUART_DEVICE_ID  0x0936

#define QRK_HSUART_REG_INTR_PIN 0x3D
#define UART_IER    1
#define UART_FCR    2
#define UART_LCR    3
#define UART_MCR    4
#define UART_DLL    0
#define UART_DLH    1
#define UART_LCR_DLAB    0x80

static struct pci_dev hsuart;
static struct ioaddr __hsuart_ioaddr = {
    .addr = 0xCF00B000,
    .type = IOADDR_MMIO32,
};

void quark_hsuart_setup()
{
    printk("Quark SoC: Initalizing HSUART\n");

    if (pci_device_scan(QRK_HSUART_VENDOR_ID, QRK_HSUART_DEVICE_ID, &hsuart)) {
        panic("Quark SoC: HSUART not found!\n");
    }

    printk("Quark SoC: HSUART [Bus: %d, Device: %d, Function: %d]\n", hsuart.bus, hsuart.dev, hsuart.func);

    uint8_t intr_pin = pci_reg8_read(&hsuart, QRK_HSUART_REG_INTR_PIN);
    printk("Quark SoC: HSUART: Interrupt Pin INT%c#\n", 'A' + intr_pin - 1);
    //quark_root_complex_iofabric_route(intr_pin, 4);
    
    quark_root_complex_io_fabric_route(1, 4);
    quark_root_complex_io_fabric_route(2, 4);
    quark_root_complex_io_fabric_route(3, 4);
    quark_root_complex_io_fabric_route(4, 4);

    /* Switch HSUART to Interrup mode */

    printk("Quark SoC: HSUART: Switching to FIFO Interrupt-Mode Operation\n");

    /* Wait until all transmission ends */
    while (!(io_in8(&__hsuart_ioaddr, 5) & 0x20));

    io_out8(&__hsuart_ioaddr, UART_FCR, 0x07);    /* Enable FIFO, Rx trigger when 1 character in */
    io_out8(&__hsuart_ioaddr, UART_IER, 0x01);    /* Enable Rx interrupt */
}


#define PIT_IRQ 0
void chipset_timer_setup(size_t period_ns, void (*handler)())
{
    //pit_setup(timer_freq);
    x86_irq_handler_install(PIT_IRQ, handler);
    //hpet_timer_setup(1, x86_sched_handler);
}

int chipset_init()
{
    earlycon_init();
    printk("Quark SoC: Welcome to AquilaOS!\n");

    quark_pci_init();
    quark_legacy_bridge_setup();
    quark_pic_setup();
    quark_hsuart_setup();

    printk("Try it!\n");

    for (;;)
        asm volatile ("sti; hlt; cli;");

    for (;;);

    return 0;
}
