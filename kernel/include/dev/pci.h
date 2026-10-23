#ifndef _DEV_PCI_H
#define _DEV_PCI_H

#include <core/system.h>

struct pci_dev;

#include <dev/dev.h>
#include <cpu/io.h>
#include <dev/irq.h>

struct pci_dev {
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
};

extern struct dev pcidev;

/**
 * The host bridge, the platform's (platform_init() sets it): how its
 * configuration space is reached, which IRQs its devices' interrupt pins
 * come to, and where their registers go if no firmware put them.
 */
struct pci_host {
    const char *name;

    /* the configuration space: ports (mechanism #1, conf1.c), or memory
     * (ECAM, ecam.c: `size' of it from `config', mapped by pci_host_set) */
    struct ioaddr config;
    size_t size;
    uint32_t (*read)(struct pci_host *host, struct pci_dev *dev, uint16_t reg);
    void     (*write)(struct pci_host *host, struct pci_dev *dev, uint16_t reg, uint32_t val);

    /* INTA# to INTD#'s IRQs, a device's swizzled by its slot; none (0):
     * the interrupt line register's, the firmware's */
    int      nirq;
    unsigned irq[4];

    /* the BARs nothing assigned (no firmware: the virt machine) go
     * there, in bus addresses; none (0 size): the firmware did */
    uint64_t mem_base, mem_size;
    uint64_t io_base, io_size;
};

int pci_host_set(struct pci_host *host);

/* dev/pci/conf1.c: configuration mechanism #1, two ports (0xCF8) */
uint32_t pci_conf1_read(struct pci_host *host, struct pci_dev *dev, uint16_t reg);
void     pci_conf1_write(struct pci_host *host, struct pci_dev *dev, uint16_t reg, uint32_t val);

/* dev/pci/ecam.c: the Enhanced Configuration Access Mechanism, memory */
uint32_t pci_ecam_read(struct pci_host *host, struct pci_dev *dev, uint16_t reg);
void     pci_ecam_write(struct pci_host *host, struct pci_dev *dev, uint16_t reg, uint32_t val);

/*
 * PCI Interface
 */

int      pci_scan_device(uint8_t class, uint8_t subclass, struct pci_dev *_dev, size_t nr);
int      pci_device_scan(uint16_t vendor_id, uint16_t device_id, struct pci_dev *ref);
uint8_t  pci_reg8_read(struct pci_dev *dev, uint8_t off);
uint16_t pci_reg16_read(struct pci_dev *dev, uint8_t off);
uint32_t pci_reg32_read(struct pci_dev *dev, uint8_t off);
void     pci_reg8_write(struct pci_dev *dev, uint8_t off, uint8_t val);
void     pci_reg16_write(struct pci_dev *dev, uint8_t off, uint16_t val);
void     pci_reg32_write(struct pci_dev *dev, uint8_t off, uint32_t val);
uint32_t pci_read_bar(struct pci_dev *dev, uint8_t id);

/** a base address register: what it decodes, where, how much */
struct pci_bar {
    uint64_t addr;
    uint64_t size;
    int io;         /* I/O ports, else memory */
};

int      pci_bar(struct pci_dev *dev, int id, struct pci_bar *bar);
void     pci_enable(struct pci_dev *dev);
int      pci_irq(struct pci_dev *dev);

#define PCI_CAP_MSI     0x05
#define PCI_CAP_MSIX    0x11

uint8_t  pci_cap_find(struct pci_dev *dev, uint8_t id);
int      pci_msi_enable(struct pci_dev *dev, irq_handler_t handler, void *arg);

#endif /* ! _DEV_PCI_H */
