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

extern dev_t pci_bus;

#endif /* ! _PCI_H */
