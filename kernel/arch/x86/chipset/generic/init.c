#include <core/system.h>
#include <console/earlycon.h>
#include <chipset/misc.h>
#include <cpu/io.h>
#include <dev/pci.h>

#define PIC_MASTER	0x20
#define PIC_SLAVE	0xA0

int x86_generic_pic_init()
{
    struct ioaddr pic_master;
    struct ioaddr pic_slave;

    pic_master.addr = PIC_MASTER;
    pic_master.type = IOADDR_PORT;
    pic_slave.addr  = PIC_SLAVE;
    pic_slave.type  = IOADDR_PORT;

    return x86_pic_setup(&pic_master, &pic_slave);
}

void x86_generic_pci_init()
{
    printk("x86: Initializing PCI\n");
    struct ioaddr pci;
    pci.addr = 0xCF8;
    pci.type = IOADDR_PORT;
    pci_ioaddr_set(&pci);
}

#define I8042_PORT  0x60
int x86_generic_i8042_init()
{
    struct ioaddr i8042;

    i8042.addr = I8042_PORT;
    i8042.type = IOADDR_PORT;

    return x86_i8042_setup(&i8042);
}

#define PIT_CHANNEL0	0x40
int x86_generic_pit_init()
{
    struct ioaddr pit;

    pit.addr = PIT_CHANNEL0;
    pit.type = IOADDR_PORT;

    x86_pit_setup(&pit);

    return 0;
}

#define PIT_IRQ 0
void chipset_timer_setup(size_t period_ns, void (*handler)())
{
    x86_pit_period_set(1);
    //hpet_timer_setup(1, handler);
    
    x86_irq_handler_install(PIT_IRQ, handler);
}

int chipset_init()
{
    earlycon_init();
    printk("x86: Welcome to AquilaOS!\n");

    x86_generic_pci_init();
    x86_generic_pic_init();
    x86_generic_i8042_init();
    x86_generic_pit_init();

    return 0;
}
