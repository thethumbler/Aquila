/** \file
 * The interrupt controller: ARM's GICv2
 *
 * References:
 *      [1] - ARM Generic Interrupt Controller Architecture Specification,
 *            version 2.0 (IHI 0048B): the distributor (GICD_*), the CPU
 *            interface (GICC_*)
 *
 * The distributor takes the interrupts (SPIs, a device's; PPIs, a CPU's
 * own, its timer's) and sends them to the CPUs' interfaces; a CPU reads
 * GICC_IAR for which came (it's acknowledged), and writes it to GICC_EOIR
 * once handled. Everything to the boot CPU, all of the same priority.
 */

#include <core/system.h>
#include <cpu/io.h>
#include <platform/virt.h>

#define GICD_CTLR           0x000
#define GICD_ISENABLER(n)   (0x100 + 4 * (n))
#define GICD_ICENABLER(n)   (0x180 + 4 * (n))
#define GICD_ICENABLER(n)   (0x180 + 4 * (n))
#define GICD_IPRIORITYR(n)  (0x400 + 4 * (n))
#define GICD_ITARGETSR(n)   (0x800 + 4 * (n))
#define GICD_ICFGR(n)       (0xC00 + 4 * (n))

#define GICC_CTLR           0x000
#define GICC_PMR            0x004
#define GICC_IAR            0x00C
#define GICC_EOIR           0x010

static uintptr_t gicd = VIRT_REG(VIRT_GICD);
static uintptr_t gicc = VIRT_REG(VIRT_GICC);

void gic_setup(void)
{
    mmio_write32(gicd + GICD_CTLR, 0);

    /* every interrupt masked, of the middle priority, to CPU 0 */
    for (unsigned i = 0; i < IRQ_NR / 32; ++i)
        mmio_write32(gicd + GICD_ICENABLER(i), 0xFFFFFFFF);

    for (unsigned i = 0; i < IRQ_NR / 4; ++i) {
        mmio_write32(gicd + GICD_IPRIORITYR(i), 0xA0A0A0A0);

        if (i >= 8)     /* SPIs (the PPIs' targets are their CPU's) */
            mmio_write32(gicd + GICD_ITARGETSR(i), 0x01010101);
    }

    mmio_write32(gicd + GICD_CTLR, 1);

    /* the CPU interface: every priority through */
    mmio_write32(gicc + GICC_PMR, 0xF0);
    mmio_write32(gicc + GICC_CTLR, 1);
}

void gic_enable(unsigned irq)
{
    mmio_write32(gicd + GICD_ISENABLER(irq / 32), 1U << (irq % 32));
}

void gic_disable(unsigned irq)
{
    mmio_write32(gicd + GICD_ICENABLER(irq / 32), 1U << (irq % 32));
}

unsigned gic_ack(void)
{
    return mmio_read32(gicc + GICC_IAR) & 0x3FF;
}

void gic_eoi(unsigned irq)
{
    mmio_write32(gicc + GICC_EOIR, irq);
}
