#include <core/kargs.h>
#include <core/system.h>
#include <cpu/io.h>
#include <dev/pci.h>
#include <platform/misc.h>

#define PIC_MASTER  0x20
#define PIC_SLAVE   0xA0

static int x86_pc_pic_init(void)
{
    struct ioaddr pic_master;
    struct ioaddr pic_slave;

    pic_master.addr = PIC_MASTER;
    pic_master.type = IOADDR_PORT;
    pic_slave.addr  = PIC_SLAVE;
    pic_slave.type  = IOADDR_PORT;

    return x86_pic_setup(&pic_master, &pic_slave);
}

static void x86_pc_pci_init(void)
{
    printk("x86: Initializing PCI\n");
    struct ioaddr pci;
    pci.addr = 0xCF8;
    pci.type = IOADDR_PORT;
    pci_ioaddr_set(&pci);
}

#define I8042_PORT  0x60
static int x86_pc_i8042_init(void)
{
    struct ioaddr i8042;

    i8042.addr = I8042_PORT;
    i8042.type = IOADDR_PORT;

    return x86_i8042_setup(&i8042);
}

#define PIT_CHANNEL0    0x40
static int x86_pc_pit_init(void)
{
    struct ioaddr pit;

    pit.addr = PIT_CHANNEL0;
    pit.type = IOADDR_PORT;

    x86_pit_setup(&pit);

    return 0;
}

#define PIT_IRQ 0
void platform_timer_setup(size_t period_ns, void (*handler)())
{
    x86_pit_period_set(200000);
    //hpet_timer_setup(1, handler);
    
    x86_irq_handler_install(PIT_IRQ, handler);
}

int platform_init(void)
{
    x86_pc_pci_init();
    x86_pc_pic_init();
    x86_pc_i8042_init();
    x86_pc_pit_init();
    
    return 0;
}
