/** \file
 * PCI configuration space through ECAM, the Enhanced Configuration Access
 * Mechanism of PCI Express.
 *
 * Every function's 4 KiB of configuration space is in memory, one after
 * the other, ordered by bus, device and function, from the host bridge's
 * `config` address (which `pci_host_set()` mapped). Reading or writing a
 * register is a plain memory access.
 */

#include <core/system.h>
#include <dev/pci.h>
#include <cpu/io.h>

/** Returns where register `reg` of `dev` is, from the start of the
 * configuration space (the register rounded down to 4 bytes). */
static uintptr_t offset(struct pci_dev *dev, uint16_t reg)
{
    return (uintptr_t) dev->bus << 20 | (uintptr_t) dev->dev << 15 |
           (uintptr_t) dev->func << 12 | (reg & 0xFFC);
}

/**
 * Reads a 32-bit register of a function's configuration space.
 *
 * @param host  the host bridge
 * @param dev   the function
 * @param reg   the register's offset (rounded down to 4 bytes)
 * @return the register's value, or all ones if it is past what the host
 *         mapped
 */
uint32_t pci_ecam_read(struct pci_host *host, struct pci_dev *dev, uint16_t reg)
{
    uintptr_t off = offset(dev, reg);

    if (off >= host->size)
        return 0xFFFFFFFF;

    return io_in32(&host->config, off);
}

/**
 * Writes a 32-bit register of a function's configuration space. A
 * register past what the host mapped is left alone.
 *
 * @param host  the host bridge
 * @param dev   the function
 * @param reg   the register's offset (rounded down to 4 bytes)
 * @param val   the value to write
 */
void pci_ecam_write(struct pci_host *host, struct pci_dev *dev, uint16_t reg, uint32_t val)
{
    uintptr_t off = offset(dev, reg);

    if (off < host->size)
        io_out32(&host->config, off, val);
}
