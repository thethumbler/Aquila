/** \file
 * The Enhanced Configuration Access Mechanism (ECAM, PCI Express): every
 * function's 4 KiB of configuration space in memory, one after another
 * (bus, device, function), from the host's config (mapped).
 */

#include <core/system.h>
#include <dev/pci.h>
#include <cpu/io.h>

static uintptr_t offset(struct pci_dev *dev, uint16_t reg)
{
    return (uintptr_t) dev->bus << 20 | (uintptr_t) dev->dev << 15 |
           (uintptr_t) dev->func << 12 | (reg & 0xFFC);
}

uint32_t pci_ecam_read(struct pci_host *host, struct pci_dev *dev, uint16_t reg)
{
    uintptr_t off = offset(dev, reg);

    if (off >= host->size)
        return 0xFFFFFFFF;

    return io_in32(&host->config, off);
}

void pci_ecam_write(struct pci_host *host, struct pci_dev *dev, uint16_t reg, uint32_t val)
{
    uintptr_t off = offset(dev, reg);

    if (off < host->size)
        io_out32(&host->config, off, val);
}
