#ifndef _DEV_PCI_H
#define _DEV_PCI_H

#include <core/system.h>

struct pci_dev;

#include <dev/dev.h>
#include <cpu/io.h>
#include <dev/irq.h>

/*
 * The PCI bus (dev/pci/): finding devices, their configuration space,
 * their BARs, and their interrupts. See docs/kernel/devices.md.
 */

/** A PCI function: its bus, device (slot) and function numbers. */
struct pci_dev {
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
};

extern struct dev pcidev;

/**
 * The host bridge, which the platform describes (`platform_init()` sets
 * it): how its configuration space is reached, which IRQs its devices'
 * interrupt pins are routed to, and where their registers go if no
 * firmware placed them.
 */
struct pci_host {
    const char *name;

    /* the configuration space: ports (mechanism #1, conf1.c), or memory
     * (ECAM, ecam.c: `size` bytes from `config`, which pci_host_set()
     * maps) */
    struct ioaddr config;
    size_t size;
    uint32_t (*read)(struct pci_host *host, struct pci_dev *dev, uint16_t reg);
    void     (*write)(struct pci_host *host, struct pci_dev *dev, uint16_t reg, uint32_t val);

    /* the IRQs that INTA# to INTD# are routed to, each device's pins
     * rotated by its slot; with none (0), a device's IRQ is what the
     * firmware wrote in its interrupt line register */
    int      nirq;
    unsigned irq[4];

    /* where the BARs that nothing assigned go (when no firmware ran, as
     * on the virt machine), in bus addresses; a size of 0 means the
     * firmware assigned them */
    uint64_t mem_base, mem_size;
    uint64_t io_base, io_size;
};

/* Sets the platform's host bridge. */
int pci_host_set(struct pci_host *host);

/* dev/pci/conf1.c: configuration mechanism #1, through two ports (0xCF8) */
uint32_t pci_conf1_read(struct pci_host *host, struct pci_dev *dev, uint16_t reg);
void     pci_conf1_write(struct pci_host *host, struct pci_dev *dev, uint16_t reg, uint32_t val);

/* dev/pci/ecam.c: the Enhanced Configuration Access Mechanism, in memory */
uint32_t pci_ecam_read(struct pci_host *host, struct pci_dev *dev, uint16_t reg);
void     pci_ecam_write(struct pci_host *host, struct pci_dev *dev, uint16_t reg, uint32_t val);

/* Finding devices, and reading and writing their configuration space. */

int      pci_scan_device(uint8_t class, uint8_t subclass, struct pci_dev *_dev, size_t nr);
int      pci_device_scan(uint16_t vendor_id, uint16_t device_id, struct pci_dev *ref);
uint8_t  pci_reg8_read(struct pci_dev *dev, uint8_t off);
uint16_t pci_reg16_read(struct pci_dev *dev, uint8_t off);
uint32_t pci_reg32_read(struct pci_dev *dev, uint8_t off);
void     pci_reg8_write(struct pci_dev *dev, uint8_t off, uint8_t val);
void     pci_reg16_write(struct pci_dev *dev, uint8_t off, uint16_t val);
void     pci_reg32_write(struct pci_dev *dev, uint8_t off, uint32_t val);
uint32_t pci_read_bar(struct pci_dev *dev, uint8_t id);

/** A base address register (BAR): where the device's registers are, how
 * big they are, and whether they are ports or memory. */
struct pci_bar {
    uint64_t addr;
    uint64_t size;
    int io;         /* I/O ports, else memory */
};

/* Reads a BAR; turns on a device's decoding and bus mastering; returns
 * its IRQ. */
int      pci_bar(struct pci_dev *dev, int id, struct pci_bar *bar);
void     pci_enable(struct pci_dev *dev);
int      pci_irq(struct pci_dev *dev);

/* Capabilities' IDs: MSI, MSI-X. */
#define PCI_CAP_MSI     0x05
#define PCI_CAP_MSIX    0x11

/* Finds a capability; makes the device's interrupts messages (MSI). */
uint8_t  pci_cap_find(struct pci_dev *dev, uint8_t id);
int      pci_msi_enable(struct pci_dev *dev, irq_handler_t handler, void *arg);

#endif /* ! _DEV_PCI_H */
