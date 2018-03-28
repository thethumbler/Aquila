#ifndef _PCI_H
#define _PCI_H

#include <stdint.h>
#include <dev/dev.h>

#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC

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


/*
 * PCI Interface
 */

int pci_scan_device(uint8_t class, uint8_t subclass, struct pci_dev *_dev);

#endif /* ! _PCI_H */
