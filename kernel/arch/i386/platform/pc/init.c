#include <core/kargs.h>
#include <core/system.h>
#include <cpu/io.h>
#include <dev/pci.h>
#include <platform/misc.h>


/* PCI bus */
#define PCI_ADDR    0xCF8
#define PCI_TYPE    IOADDR_PORT

/* i8042 PS/2 controller */
#define I8042_ADDR  0x60
#define I8042_TYPE  IOADDR_PORT

/* i8254 PIT controller */
#define I8254_ADDR  0x40
#define I8254_TYPE  IOADDR_PORT

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

    pci.addr = PCI_ADDR;
    pci.type = PCI_TYPE;

    pci_ioaddr_set(&pci);
}

static int x86_pc_i8042_init(void)
{
    struct ioaddr i8042;

    i8042.addr = I8042_ADDR;
    i8042.type = I8042_TYPE;

    return x86_i8042_setup(&i8042);
}

static int x86_pc_pit_init(void)
{
    struct ioaddr pit;

    pit.addr = I8254_ADDR;
    pit.type = I8254_TYPE;

    return x86_pit_setup(&pit);
}

static int x86_pc_cmos_init(void)
{
    struct ioaddr cmos;
    cmos.addr = 0x70;
    cmos.type = IOADDR_PORT;

    return x86_cmos_setup(&cmos);
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
    x86_pc_cmos_init();
    
    return 0;
}
