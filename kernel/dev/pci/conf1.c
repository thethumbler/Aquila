/** \file
 * PCI configuration space through configuration mechanism #1, the PC's.
 *
 * The address of a register (its bus, device, function and offset) is
 * written to one port, `CONFIG_ADDRESS` (the host bridge's `config`,
 * `0xCF8` on the PC), and the register's value is then read from or
 * written to the next one, `CONFIG_DATA`, 4 bytes on. Only the first 256
 * bytes of a function's configuration space can be reached this way.
 */

#include <core/system.h>
#include <dev/pci.h>
#include <cpu/io.h>

#define PCI_CONFIG_ADDRESS  0x00
#define PCI_CONFIG_DATA     0x04

/** What is written to `CONFIG_ADDRESS`: the register to reach. */
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

/** Selects register `reg` of `dev` (rounded down to 4 bytes), for the
 * next access to `CONFIG_DATA`. */
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

/**
 * Reads a 32-bit register of a function's configuration space.
 *
 * @param host  the host bridge
 * @param dev   the function
 * @param reg   the register's offset (rounded down to 4 bytes)
 * @return the register's value, or all ones for an offset past 255
 */
uint32_t pci_conf1_read(struct pci_host *host, struct pci_dev *dev, uint16_t reg)
{
    if (reg > 0xFF)
        return 0xFFFFFFFF;

    conf1_select(host, dev, reg);
    return io_in32(&host->config, PCI_CONFIG_DATA);
}

/**
 * Writes a 32-bit register of a function's configuration space. An offset
 * past 255 is left alone.
 *
 * @param host  the host bridge
 * @param dev   the function
 * @param reg   the register's offset (rounded down to 4 bytes)
 * @param val   the value to write
 */
void pci_conf1_write(struct pci_host *host, struct pci_dev *dev, uint16_t reg, uint32_t val)
{
    if (reg > 0xFF)
        return;

    conf1_select(host, dev, reg);
    io_out32(&host->config, PCI_CONFIG_DATA, val);
}
