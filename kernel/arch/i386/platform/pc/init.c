/** \file
 * The PC platform: its interrupt controllers, its timers, its PCI bus
 * and its real time clock, set up at boot, and the tick.
 */

#include <core/kargs.h>
#include <core/system.h>
#include <cpu/io.h>
#include <dev/pci.h>
#include <platform/misc.h>

/* the 8254 PIT's ports */
#define I8254_ADDR  0x40
#define I8254_TYPE  IOADDR_PORT

/* the 8259s' ports */
#define PIC_MASTER  0x20
#define PIC_SLAVE   0xA0

/** Sets up the 8259s at the PC's ports. Returns 0, or -ENODEV. */
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

/* The PCI bus, through configuration mechanism #1's ports. The firmware
 * set up its devices' registers and interrupt lines. */
static struct pci_host x86_pc_pci = {
    .name   = "configuration mechanism #1",
    .config = {.type = IOADDR_PORT, .addr = 0xCF8},
    .read   = pci_conf1_read,
    .write  = pci_conf1_write,
};

/** Gives PCI its host bridge. */
static void x86_pc_pci_init(void)
{
#ifdef CONFIG_DEV_PCI
    pci_host_set(&x86_pc_pci);
#endif
}

/** Sets up the PIT at the PC's ports. */
static int x86_pc_pit_init(void)
{
    struct ioaddr pit;

    pit.addr = I8254_ADDR;
    pit.type = I8254_TYPE;

    return x86_pit_setup(&pit);
}

/** Sets up the real time clock at the PC's ports. */
static int x86_pc_cmos_init(void)
{
    struct ioaddr cmos;
    cmos.addr = 0x70;
    cmos.type = IOADDR_PORT;

    return x86_cmos_setup(&cmos);
}

#define PIT_IRQ 0

static int apic;

/**
 * Starts the scheduler's tick: the local APIC's timer if the APIC brings
 * the interrupts, else the PIT.
 *
 * @param period_ns  the tick's period, in ns
 * @param handler    the tick's handler
 * @return the period it really is, in ns
 */
uint32_t platform_timer_setup(size_t period_ns, irq_handler_t handler)
{
    if (apic)
        return x86_lapic_timer_setup(period_ns, handler);

    uint32_t period = x86_pit_period_set(period_ns);
    irq_install(PIT_IRQ, handler, NULL);
    return period;
}

/** Sets up the interrupts: through the APIC if there's one (and not
 * `apic=off`), else through the 8259s. The 8259s are remapped either way. */
static void x86_pc_irq_init(void)
{
    x86_irq_gates_setup();

    int pic = x86_pc_pic_init();

    apic = x86_apic_setup && !x86_apic_setup();

    if (!apic) {
        if (pic)
            printk("x86: no interrupt controller\n");
        else
            x86_irq_chip_set(&x86_pic_chip);
    }

    printk("x86: interrupts through the %s\n", x86_irq_chip_name());
}

/*
 * Sets up the PC platform at boot: PCI, the interrupt controllers, the
 * timers, the real time clock, then the other CPUs.
 */
int platform_init(void)
{
    x86_pc_pci_init();
    x86_pc_irq_init();
    x86_pc_pit_init();
    x86_pc_cmos_init();

    /* start the other CPUs (the PIT times their start) */
    if (apic)
        x86_smp_init();

    return 0;
}
