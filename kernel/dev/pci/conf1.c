/** \file
 * PCI configuration mechanism #1: the address of a register written to
 * one port (CONFIG_ADDRESS, the host's config), its value read from or
 * written to the next (CONFIG_DATA, 4 bytes on). The PC's.
 */

#include <core/system.h>
#include <dev/pci.h>
#include <cpu/io.h>

#define PCI_CONFIG_ADDRESS  0x00
#define PCI_CONFIG_DATA     0x04

union pci_address {
    struct  {
        uint8_t reg      : 8;
        uint8_t function : 3;
        uint8_t device   : 5;
        uint8_t bus      : 8;
        uint8_t reserved : 7;
        uint8_t enable   : 1;
    } structure;
    uint32_t raw;
} __packed;

/** the register's address (the first 256 bytes only, 4-byte aligned) */
static void conf1_select(struct pci_host *host, struct pci_dev *dev, uint16_t reg)
{
    union pci_address addr;
    addr.raw = 0;

    addr.structure.enable = 1;
    addr.structure.bus = dev->bus;
    addr.structure.device = dev->dev;
    addr.structure.function = dev->func;
    addr.structure.reg = reg & 0xfc;

    io_out32(&host->config, PCI_CONFIG_ADDRESS, addr.raw);
}

uint32_t pci_conf1_read(struct pci_host *host, struct pci_dev *dev, uint16_t reg)
{
    if (reg > 0xFF)
        return 0xFFFFFFFF;

    conf1_select(host, dev, reg);
    return io_in32(&host->config, PCI_CONFIG_DATA);
}

void pci_conf1_write(struct pci_host *host, struct pci_dev *dev, uint16_t reg, uint32_t val)
{
    if (reg > 0xFF)
        return;

    conf1_select(host, dev, reg);
    io_out32(&host->config, PCI_CONFIG_DATA, val);
}
