#include <core/system.h>
#include <chipset/legacy_bridge.h>
#include <chipset/root_complex.h>

#define QRK_LEG_BRDG_REG_RCBA   0xF0
#define QRK_LEG_BRDG_REG_PIRQx  0x60

#define QRK_LEG_BRDG_RC_IRQAGENT0  0x3140
#define QRK_LEG_BRDG_RC_IRQAGENT1  0x3142
#define QRK_LEG_BRDG_RC_IRQAGENT2  0x3144
#define QRK_LEG_BRDG_RC_IRQAGENT3  0x3146

static char __rcrb[4 * PAGE_SIZE] __aligned(PAGE_SIZE);
static struct ioaddr __rcrb_ioaddr = {
    .addr = (uintptr_t) __rcrb,
    .type = IOADDR_MMIO8, /* Direct offset */
};

#define PIC_ELCR   0x4D0
void quark_root_complex_io_fabric_route(int intr_pin, int irq)
{
    if (intr_pin < 1 || intr_pin > 4)
        panic("Invalid INTR_PIN");

    uint32_t irqagent3 = io_in16(&__rcrb_ioaddr, QRK_LEG_BRDG_RC_IRQAGENT3);
    printk("irqagent3 %x\n", irqagent3);

    uint32_t pirq = ((irqagent3) >> (intr_pin - 1) * 4) & 0x7;

    printk("Quark SoC: Root Complex: IO Fabric INT%c# is mapped to PIRQ%c\n", 'A' + intr_pin - 1, 'A' + pirq);
    printk("Quark SoC: Root Complex: Routing PIRQ%c to IRQ%d\n", 'A' + pirq, irq);

    uint8_t pirq_val = irq | (1 << 7);
    pci_reg8_write(&qrk_leg_brdg, QRK_LEG_BRDG_REG_PIRQx + pirq, pirq_val);

#if 0
    printk("Quark SoC: Root Complex: Setting PIC IRQ %d to level sensetive\n", irq);
    struct __ioaddr io = {
        .addr = 0x4D0,
        .type = __IOADDR_PORT,
    };

    if (irq >= 3 && irq <= 7) {
        uint8_t elc = __io_in8(&io, 0); /* ELCR1 */
        printk("ELCR1 %x\n", elc);
        elc |= 1 << irq;
        __io_out8(&io, 0, elc);
    } else if ((irq >= 9 && irq <= 12) || irq == 14 || irq == 15) {
        uint8_t elc = __io_in8(&io, 1); /* ELCR2 */
        printk("ELCR2 %x\n", elc);
        elc |= 1 << (irq - 8);
        __io_out8(&io, 1, elc);
    } else {
        panic("Invalid IRQ\n");
    }
#endif
}

void quark_root_complex_init()
{
    uint32_t rcba = pci_reg32_read(&qrk_leg_brdg, QRK_LEG_BRDG_REG_RCBA);

    if (!(rcba & 1)) {
        /* Enable Root Complex */
        rcba |= 1;
        pci_reg32_write(&qrk_leg_brdg, QRK_LEG_BRDG_REG_RCBA, rcba);
    }

    rcba &= ~1;

    printk("Quark SoC: Legacy Bridge: Root Complex Base Address %x\n", rcba);
    printk("Quark SoC: Legacy Bridge: Mapping RCRB to %p\n", __rcrb);
    pmman.map_to(rcba, (uintptr_t) __rcrb, 4 * PAGE_SIZE, VM_KRW);
}
