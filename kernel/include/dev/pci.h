#ifndef _DEV_PCI_H
#define _DEV_PCI_H

#include <core/system.h>

union pci_address;
struct pci_dev;

#include <dev/dev.h>
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

struct pci_dev {
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
};

extern struct dev pci_bus;
void pci_ioaddr_set(struct ioaddr *io);

/*
 * PCI Interface
 */

int      pci_scan_device(uint8_t class, uint8_t subclass, struct pci_dev *_dev, size_t nr);
int      pci_device_scan(uint16_t vendor_id, uint16_t device_id, struct pci_dev *ref);
uint8_t  pci_reg8_read(struct pci_dev *dev, uint8_t off);
uint16_t pci_reg16_read(struct pci_dev *dev, uint8_t off);
uint32_t pci_reg32_read(struct pci_dev *dev, uint8_t off);
void     pci_reg8_write(struct pci_dev *dev, uint8_t off, uint8_t val);
void     pci_reg16_write(struct pci_dev *dev, uint8_t off, uint8_t val);
void     pci_reg32_write(struct pci_dev *dev, uint8_t off, uint8_t val);
uint32_t pci_read_bar(struct pci_dev *dev, uint8_t id);

#endif /* ! _DEV_PCI_H */
