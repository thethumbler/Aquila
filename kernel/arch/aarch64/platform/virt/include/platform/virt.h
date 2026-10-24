#ifndef _PLATFORM_VIRT_H
#define _PLATFORM_VIRT_H

#include <core/system.h>
#include <mm/mm.h>

/*
 * QEMU's virt machine (hw/arm/virt.c): where its devices are, as its
 * devicetree also says. They are in the first GiB of physical memory,
 * which is device memory, mapped in the direct map from boot
 * (boot/head.S).
 */
#define VIRT_GICD       0x08000000  /* GICv2 distributor */
#define VIRT_GICC       0x08010000  /* GICv2 CPU interface */
#define VIRT_UART       0x09000000  /* PL011 */
#define VIRT_RTC        0x09010000  /* PL031 */
#define VIRT_PCIE_MMIO  0x10000000  /* PCIe: memory for BARs (the same bus addresses) */
#define VIRT_PCIE_MMIO_SIZE 0x2EFF0000
#define VIRT_PCIE_PIO   0x3EFF0000  /* PCIe: its I/O ports, 64 KiB */
#define VIRT_PCIE_ECAM  0x4010000000ULL /* PCIe: configuration space */

/* Interrupt IDs: the SPIs (shared) start at 32, and the PPIs (a CPU's
 * own) are 16 to 31. */
#define VIRT_UART_IRQ   (32 + 1)
#define VIRT_PCIE_IRQ   (32 + 3)    /* INTA# to INTD#: the 4 from here, level-triggered */
#define VIRT_TIMER_IRQ  27          /* the virtual timer (EL1) */

/* A device's registers, in the direct map: the boot tables map the
 * devices there (arch_mmio_map() maps them elsewhere). */
#define VIRT_REG(base)  ((uintptr_t) (ARCH_DMAP_BASE + (base)))

/* Sets up the platform: the interrupt controller, PCI (init.c). */
int  platform_init(void);

/* The GICv2 interrupt controller (gic.c). */
void     gic_setup(void);
void     gic_enable(unsigned irq);
void     gic_disable(unsigned irq);
unsigned gic_ack(void);             /* the interrupt that came, or 1023 for none */
void     gic_eoi(unsigned irq);

/* The interrupts' handlers (irq.c; irq_install() is in dev/irq.h). */
#define IRQ_NR          128
void irq_handle(void);              /* runs the handlers of the interrupt that came */

/* Starts the generic timer's tick, every `period_ns` (timer.c). */
uint64_t timer_setup(uint64_t period_ns, void (*handler)(void));

#endif /* ! _PLATFORM_VIRT_H */
