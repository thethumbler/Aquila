/** \file
 * QEMU's virt machine: setting up its interrupt controller and its PCI
 * Express host bridge.
 */

#include <core/system.h>
#include <dev/pci.h>
#include <platform/virt.h>

/* The PCIe host bridge (pci-host-ecam-generic), with bus 0's
 * configuration space. No firmware runs before the kernel (QEMU's
 * -kernel), so the kernel assigns the BARs itself: in the memory window,
 * and ports from 0x1000 (not 0, which means unassigned). */
static struct pci_host virt_pci = {
    .name     = "ecam",
    .config   = {.type = IOADDR_MMIO8, .addr = VIRT_PCIE_ECAM},
    .size     = 1 << 20,
    .read     = pci_ecam_read,
    .write    = pci_ecam_write,
    .nirq     = 4,
    .irq      = {VIRT_PCIE_IRQ, VIRT_PCIE_IRQ + 1, VIRT_PCIE_IRQ + 2, VIRT_PCIE_IRQ + 3},
    .mem_base = VIRT_PCIE_MMIO,
    .mem_size = VIRT_PCIE_MMIO_SIZE,
    .io_base  = 0x1000,
    .io_size  = 0x10000 - 0x1000,
};

/*
 * Sets up the platform at boot: the GIC, and the PCI host bridge.
 */
int platform_init(void)
{
    gic_setup();
    printk("virt: GICv2, interrupts through it\n");

#ifdef CONFIG_DEV_PCI
    pci_host_set(&virt_pci);
#endif

    return 0;
}
