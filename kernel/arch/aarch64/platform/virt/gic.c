/** \file
 * The virt machine's interrupt controller: ARM's GICv2.
 *
 * References:
 *      [1] - ARM Generic Interrupt Controller Architecture Specification,
 *            version 2.0 (IHI 0048B): the distributor (GICD_*), the CPU
 *            interface (GICC_*)
 *
 * The distributor takes the interrupts (SPIs, the devices'; PPIs, a CPU's
 * own, such as its timer's) and sends them to the CPUs' interfaces. A CPU
 * reads GICC_IAR to learn which interrupt came (which acknowledges it),
 * and writes it to GICC_EOIR once it's handled. Every interrupt goes to
 * the boot CPU, and all have the same priority.
 */

#include <core/system.h>
#include <cpu/io.h>
#include <platform/virt.h>

/* The distributor's registers, and the CPU interface's. */
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

/** Sets up the GIC at boot: every interrupt masked, of the same priority,
 * and sent to CPU 0. */
void gic_setup(void)
{
    mmio_write32(gicd + GICD_CTLR, 0);

    /* every interrupt masked, of a middle priority, sent to CPU 0 */
    for (unsigned i = 0; i < IRQ_NR / 32; ++i)
        mmio_write32(gicd + GICD_ICENABLER(i), 0xFFFFFFFF);

    for (unsigned i = 0; i < IRQ_NR / 4; ++i) {
        mmio_write32(gicd + GICD_IPRIORITYR(i), 0xA0A0A0A0);

        if (i >= 8)     /* SPIs only (a PPI's target is its own CPU) */
            mmio_write32(gicd + GICD_ITARGETSR(i), 0x01010101);
    }

    mmio_write32(gicd + GICD_CTLR, 1);

    /* the CPU interface lets every priority through */
    mmio_write32(gicc + GICC_PMR, 0xF0);
    mmio_write32(gicc + GICC_CTLR, 1);
}

/**
 * Enables (unmasks) an interrupt.
 *
 * @param irq  its interrupt ID
 */
void gic_enable(unsigned irq)
{
    mmio_write32(gicd + GICD_ISENABLER(irq / 32), 1U << (irq % 32));
}

/**
 * Disables (masks) an interrupt.
 *
 * @param irq  its interrupt ID
 */
void gic_disable(unsigned irq)
{
    mmio_write32(gicd + GICD_ICENABLER(irq / 32), 1U << (irq % 32));
}

/**
 * Acknowledges the interrupt that came.
 *
 * @return its interrupt ID, or 1023 if there was none (a spurious one)
 */
unsigned gic_ack(void)
{
    return mmio_read32(gicc + GICC_IAR) & 0x3FF;
}

/**
 * Tells the GIC that an interrupt has been handled (end of interrupt).
 *
 * @param irq  its interrupt ID
 */
void gic_eoi(unsigned irq)
{
    mmio_write32(gicc + GICC_EOIR, irq);
}
