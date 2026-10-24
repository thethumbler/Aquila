/** \file
 * The PCI bus: finding devices, and reading and writing their
 * configuration space.
 *
 * Every PCI function has a configuration space: who made it (vendor and
 * device IDs), what it is (class and subclass), which memory and ports it
 * decodes (its base address registers, BARs), its interrupt, and a list
 * of capabilities. How that space is reached is the platform's: the host
 * bridge (`struct pci_host`) that `platform_init()` gives
 * `pci_host_set()`, with configuration mechanism #1 (`conf1.c`) or ECAM
 * (`ecam.c`). Only bus 0 is scanned. See docs/kernel/devices.md.
 */

#include <core/system.h>
#include <core/module.h>
#include <core/arch.h>

#include <dev/pci.h>
#include <cpu/io.h>
#include <mm/mm.h>

/* the platform's host bridge; without one, there are no devices */
static struct pci_host *host;

/** Reads a 32-bit configuration register, or returns all ones (no
 * device) if there is no host bridge. */
static inline uint32_t pci_read_dword(uint8_t bus, uint8_t dev, uint8_t func, uint16_t reg)
{
    struct pci_dev d = {.bus = bus, .dev = dev, .func = func};
    return host ? host->read(host, &d, reg) : 0xFFFFFFFF;
}

/** Writes a 32-bit configuration register, if there is a host bridge. */
static inline void pci_write_dword(uint8_t bus, uint8_t dev, uint8_t func, uint16_t reg, uint32_t val)
{
    struct pci_dev d = {.bus = bus, .dev = dev, .func = func};

    if (host)
        host->write(host, &d, reg, val);
}

/** Returns a function's vendor ID: all ones if there's no function. */
static inline uint16_t get_vendor_id(uint8_t bus, uint8_t dev, uint8_t func)
{
    return pci_read_dword(bus, dev, func, 0x00) & 0xFFFF;
}

/** Returns a function's device ID. */
static inline uint16_t get_device_id(uint8_t bus, uint8_t dev, uint8_t func)
{
    return (pci_read_dword(bus, dev, func, 0x00) >> 16) & 0xFFFF;
}

/** Returns a function's class code. */
static inline uint8_t get_class_code(uint8_t bus, uint8_t dev, uint8_t func)
{
    return (pci_read_dword(bus, dev, func, 0x08) >> 24) & 0xFF;
}

/** Returns a function's subclass code. */
static inline uint8_t get_subclass_code(uint8_t bus, uint8_t dev, uint8_t func)
{
    return (pci_read_dword(bus, dev, func, 0x08) >> 16) & 0xFF;
}

/** Returns a function's header type: bit 7 set for a device with several
 * functions, the rest 0 for a device and 1 for a bridge. */
static inline uint8_t get_header_type(uint8_t bus, uint8_t dev, uint8_t func)
{
    return (pci_read_dword(bus, dev, func, 0x0C) >> 16) & 0xFF;
}

/** Returns a function's base address register `id`, as it is. */
static inline uint32_t get_bar(uint8_t bus, uint8_t dev, uint8_t func, uint8_t id)
{
    return pci_read_dword(bus, dev, func, 0x10 + 4 * id);
}

/**
 * Finds a device by its vendor and device IDs, on bus 0.
 *
 * @param vendor_id  the vendor ID
 * @param device_id  the device ID
 * @param ref        if not NULL, set to the first function found
 * @return 0 if one was found, -1 if not
 */
int pci_device_scan(uint16_t vendor_id, uint16_t device_id, struct pci_dev *ref)
{
    uint8_t bus = 0;
    for (uint8_t dev = 0; dev < 32; ++dev) {
        for (uint8_t func = 0; func < 8; ++func) {
            uint16_t _vendor_id = get_vendor_id(bus, dev, func);
            if (_vendor_id != 0xFFFF) {
                uint16_t _device_id = get_device_id(bus, dev, func);
                if (_vendor_id == vendor_id && _device_id == device_id) {
                    if (ref) {
                        ref->bus = bus;
                        ref->dev = dev;
                        ref->func = func;
                    }

                    return 0;
                }
            }
        }
    }

    return -1;
}

/**
 * Reads an 8-bit configuration register.
 *
 * @param dev  the function
 * @param off  the register's offset
 * @return its value
 */
uint8_t pci_reg8_read(struct pci_dev *dev, uint8_t off)
{
    return (pci_read_dword(dev->bus, dev->dev, dev->func, off) >> ((off & 3) * 8)) & 0xFF;
}

/**
 * Reads a 16-bit configuration register.
 *
 * @param dev  the function
 * @param off  the register's offset, a multiple of 2
 * @return its value
 */
uint16_t pci_reg16_read(struct pci_dev *dev, uint8_t off)
{
    return (pci_read_dword(dev->bus, dev->dev, dev->func, off) >> ((off & 2) * 8)) & 0xFFFF;
}

/**
 * Reads a 32-bit configuration register.
 *
 * @param dev  the function
 * @param off  the register's offset, a multiple of 4
 * @return its value
 */
uint32_t pci_reg32_read(struct pci_dev *dev, uint8_t off)
{
    return pci_read_dword(dev->bus, dev->dev, dev->func, off);
}

/**
 * Writes an 8-bit configuration register: the 32-bit register it's in is
 * read, changed and written back.
 *
 * @param dev  the function
 * @param off  the register's offset
 * @param val  the value to write
 */
void pci_reg8_write(struct pci_dev *dev, uint8_t off, uint8_t val)
{
    uint32_t oval = pci_read_dword(dev->bus, dev->dev, dev->func, off);
    oval = (oval & ~(0xFFUL << (off & 3) * 8)) | ((uint32_t) val << (off & 3) * 8);
    pci_write_dword(dev->bus, dev->dev, dev->func, off, oval);
}

/**
 * Writes a 16-bit configuration register: the 32-bit register it's in is
 * read, changed and written back.
 *
 * @param dev  the function
 * @param off  the register's offset, a multiple of 2
 * @param val  the value to write
 */
void pci_reg16_write(struct pci_dev *dev, uint8_t off, uint16_t val)
{
    uint32_t oval = pci_read_dword(dev->bus, dev->dev, dev->func, off);
    oval = (oval & ~(0xFFFFUL << (off & 2) * 8)) | ((uint32_t) val << (off & 2) * 8);
    pci_write_dword(dev->bus, dev->dev, dev->func, off, oval);
}

/**
 * Writes a 32-bit configuration register.
 *
 * @param dev  the function
 * @param off  the register's offset, a multiple of 4
 * @param val  the value to write
 */
void pci_reg32_write(struct pci_dev *dev, uint8_t off, uint32_t val)
{
    pci_write_dword(dev->bus, dev->dev, dev->func, off, val);
}

/** Prints the functions of a device, for debugging. */
static void scan_device(uint8_t bus, uint8_t dev)
{
    for (uint8_t func = 0; func < 8; ++func) {
        uint16_t vendor_id = get_vendor_id(bus, dev, func);

        if (vendor_id != 0xFFFF) {
            uint32_t device_id = get_device_id(bus, dev, func);
            uint32_t class_code = get_class_code(bus, dev, func);
            uint32_t subclass_code = get_subclass_code(bus, dev, func);

            printk("Bus: %d, Device: %d, Function %d\n", bus, dev, func);
            printk("  -> Vendor ID: %x, Device ID: %x, Class: %x, Subclass: %x\n", vendor_id, device_id, class_code, subclass_code);
        }
    }
}

/** Prints the devices of a bus, for debugging (nothing calls it). */
static void scan_bus(uint8_t bus)
{
    for (uint8_t dev = 0; dev < 32; ++dev) {
        scan_device(bus, dev);
    }
}

/**
 * The PCI module's initialization, which has nothing to do: drivers find
 * their devices themselves.
 *
 * @return 0
 */
int pci_prope()
{
    return 0;
}

struct dev pcidev = {
    .probe = pci_prope,
};



/*
 * The interface drivers use
 */

/**
 * Finds the functions of a class and subclass, on bus 0.
 *
 * @param class     the class code
 * @param subclass  the subclass code
 * @param _dev      set to the functions found
 * @param nr        how many `_dev` can hold
 * @return how many were found, at most `nr`
 */
int pci_scan_device(uint8_t class, uint8_t subclass, struct pci_dev *_dev, size_t nr)
{
    size_t  idx = 0;
    uint8_t bus = 0;

    for (uint8_t dev = 0; dev < 32; ++dev) {
        for (uint8_t func = 0; func < 8; ++func) {
            uint16_t vendor_id = get_vendor_id(bus, dev, func);

            if (vendor_id != 0xFFFF) {
                uint32_t class_code = get_class_code(bus, dev, func);
                uint32_t subclass_code = get_subclass_code(bus, dev, func);

                if (class_code == class && subclass_code == subclass) {
                    _dev[idx].bus  = bus;
                    _dev[idx].dev  = dev;
                    _dev[idx].func = func;
                    ++idx;
                    --nr;

                    if (!nr)
                        return idx;
                }
            }
        }
    }

    return idx;
}

/**
 * Reads a base address register as it is, flag bits included.
 *
 * @param dev  the function
 * @param id   the BAR's number, 0 to 5
 * @return the register's value
 */
uint32_t pci_read_bar(struct pci_dev *dev, uint8_t id)
{
    return pci_read_dword(dev->bus, dev->dev, dev->func, 0x10 + 4 * id);
}

#define PCI_COMMAND         0x04
#define PCI_COMMAND_IO      0x0001
#define PCI_COMMAND_MEMORY  0x0002
#define PCI_COMMAND_MASTER  0x0004
#define PCI_INTERRUPT_LINE  0x3C

/**
 * Reads a base address register: what it decodes (memory or ports), its
 * address, and its size. The size is found by writing all ones to the
 * register and reading back what the device kept, with decoding turned
 * off meanwhile. A 64-bit memory BAR uses the next register too.
 *
 * @param dev  the function
 * @param id   the BAR's number, 0 to 5
 * @param bar  set to what the BAR decodes
 * @return 0, or -1 if the BAR decodes nothing
 */
int pci_bar(struct pci_dev *dev, int id, struct pci_bar *bar)
{
    uint8_t off = 0x10 + 4 * id;
    uint16_t command = pci_reg16_read(dev, PCI_COMMAND);
    uint32_t lo = pci_reg32_read(dev, off), hi = 0, size_lo, size_hi = 0xFFFFFFFF;

    pci_reg16_write(dev, PCI_COMMAND, command & ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY));

    pci_reg32_write(dev, off, 0xFFFFFFFF);
    size_lo = pci_reg32_read(dev, off);
    pci_reg32_write(dev, off, lo);

    bar->io = lo & 1;

    /* a 64-bit memory BAR */
    if (!bar->io && (lo & 0x6) == 0x4) {
        hi = pci_reg32_read(dev, off + 4);
        pci_reg32_write(dev, off + 4, 0xFFFFFFFF);
        size_hi = pci_reg32_read(dev, off + 4);
        pci_reg32_write(dev, off + 4, hi);
    }

    pci_reg16_write(dev, PCI_COMMAND, command);

    uint64_t mask = bar->io ? ~(uint64_t) 0x3 : ~(uint64_t) 0xF;
    uint64_t sized = ((uint64_t) size_hi << 32 | size_lo) & mask;

    if (bar->io)
        sized |= 0xFFFFFFFF00000000ULL | 0xFFFF0000; /* ports are 16 bits */

    bar->addr = ((uint64_t) hi << 32 | lo) & mask;
    bar->size = ~sized + 1;

    return size_lo ? 0 : -1;
}

/**
 * Turns a function's memory and port decoding on, and bus mastering, so
 * that it can do DMA.
 *
 * @param dev  the function
 */
void pci_enable(struct pci_dev *dev)
{
    uint16_t command = pci_reg16_read(dev, PCI_COMMAND);
    pci_reg16_write(dev, PCI_COMMAND,
        command | PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
}

#define PCI_INTERRUPT_PIN   0x3D

/**
 * Finds the IRQ that a function's interrupt pin comes to. If the host
 * bridge says which IRQs INTA# to INTD# come to, the pin is swizzled by
 * the device's slot, as a bridge's interrupt map does (INTA# of slot 1 is
 * INTB#). Otherwise it is the interrupt line that the firmware wrote.
 *
 * @param dev  the function
 * @return the IRQ, or -1 if the function has no interrupt pin
 */
int pci_irq(struct pci_dev *dev)
{
    if (!host || !host->nirq)
        return pci_reg8_read(dev, PCI_INTERRUPT_LINE);

    int pin = pci_reg8_read(dev, PCI_INTERRUPT_PIN);

    if (pin < 1 || pin > 4)
        return -1;

    return host->irq[(dev->dev + pin - 1) % host->nirq];
}

#define ALIGN_TO(x, a)      (((x) + (a) - 1) & ~((uint64_t) (a) - 1))

/** Assigns addresses to the BARs that no firmware assigned, on bus 0's
 * devices: each BAR in the host's window for its kind (memory or ports),
 * aligned to its size. The addresses are the bus's. */
static void pci_assign(void)
{
    uint64_t mem = host->mem_base, io = host->io_base;

    for (uint8_t slot = 0; slot < 32; ++slot) {
        for (uint8_t func = 0; func < 8; ++func) {
            struct pci_dev dev = {.bus = 0, .dev = slot, .func = func};

            /* skip a missing function, and a bridge (its BARs have another
             * layout) */
            if (get_vendor_id(0, slot, func) == 0xFFFF || (get_header_type(0, slot, func) & 0x7F))
                continue;

            for (int id = 0; id < 6; ++id) {
                uint8_t off = 0x10 + 4 * id;
                uint32_t lo = pci_reg32_read(&dev, off);
                int wide = !(lo & 1) && (lo & 0x6) == 0x4;
                struct pci_bar bar;

                if (!pci_bar(&dev, id, &bar) && !bar.addr) {
                    uint64_t *next = bar.io ? &io : &mem;
                    uint64_t end = bar.io ? host->io_base + host->io_size
                                          : host->mem_base + host->mem_size;
                    uint64_t addr = ALIGN_TO(*next, bar.size);

                    if (addr + bar.size <= end) {
                        pci_reg32_write(&dev, off, (uint32_t) addr | (lo & (bar.io ? 0x3 : 0xF)));

                        if (wide)
                            pci_reg32_write(&dev, off + 4, (uint32_t) (addr >> 32));

                        *next = addr + bar.size;
                        printk("pci: %d:%d.%d BAR %d: %s at %p, %ld bytes\n", 0, slot, func, id,
                               bar.io ? "ports" : "memory", (void *) (uintptr_t) addr, bar.size);
                    } else {
                        printk("pci: %d:%d.%d BAR %d: no room for %ld bytes\n", 0, slot, func, id, bar.size);
                    }
                }

                if (wide)
                    ++id;
            }

            /* a device with one function only: don't look for others */
            if (!func && !(get_header_type(0, slot, 0) & 0x80))
                break;
        }
    }
}

/**
 * Sets the platform's host bridge, from `platform_init()`. Its
 * configuration space is mapped if it is memory (ECAM), and the BARs no
 * firmware assigned get addresses in its windows.
 *
 * @param h  the host bridge
 * @return 0, or `-ENOMEM` if its configuration space couldn't be mapped
 */
int pci_host_set(struct pci_host *h)
{
    if (h->config.type != IOADDR_PORT) {
        void *config = arch_mmio_map(h->config.addr, h->size);

        if (!config)
            return -ENOMEM;

        printk("pci: %s at %p\n", h->name, (void *) h->config.addr);
        h->config.addr = (uintptr_t) config;
    } else {
        printk("pci: %s at port %p\n", h->name, (void *) h->config.addr);
    }

    host = h;

    if (h->mem_size || h->io_size)
        pci_assign();

    return 0;
}

#define PCI_STATUS              0x06
#define PCI_STATUS_CAP_LIST     0x10
#define PCI_CAPABILITIES        0x34
#define PCI_COMMAND_INTX_OFF    0x400

/**
 * Finds one of a function's capabilities.
 *
 * @param dev  the function
 * @param id   the capability (`PCI_CAP_*`)
 * @return the capability's offset in the configuration space, or 0 if
 *         the function doesn't have it
 */
uint8_t pci_cap_find(struct pci_dev *dev, uint8_t id)
{
    if (!(pci_reg16_read(dev, PCI_STATUS) & PCI_STATUS_CAP_LIST))
        return 0;

    uint8_t off = pci_reg8_read(dev, PCI_CAPABILITIES) & 0xfc;

    /* follow at most 48 of them: each is at least 4 bytes, after the
     * header, so a longer list is a broken one */
    for (int i = 0; off && i < 48; ++i) {
        if (pci_reg8_read(dev, off) == id)
            return off;

        off = pci_reg8_read(dev, off + 1) & 0xfc;
    }

    return 0;
}

/* MSI's capability: its message control, the message's address (with its
 * high half if it is 64-bit), then the message's data */
#define MSI_CTRL                2
#define MSI_ADDR                4
#define MSI_CTRL_ENABLE         0x0001
#define MSI_CTRL_MME            0x0070      /* how many messages: 0 means 1 */
#define MSI_CTRL_64BIT          0x0080

/**
 * Has a function send its interrupts as messages (MSI) instead of on its
 * interrupt line: one message, for an IRQ that goes to `handler`. Its
 * interrupt line (INTx) is turned off. If MSI isn't possible, the caller
 * uses the line instead.
 *
 * @param dev      the function
 * @param handler  the interrupt's handler
 * @param arg      the handler's argument
 * @return the IRQ, `-ENODEV` if the function or the architecture has no
 *         MSI, or another negative errno
 */
int pci_msi_enable(struct pci_dev *dev, irq_handler_t handler, void *arg)
{
    uint8_t cap = pci_cap_find(dev, PCI_CAP_MSI);
    uint64_t addr;
    uint32_t data;
    int irq;

    if (!cap || !arch_msi_alloc)
        return -ENODEV;

    if ((irq = arch_msi_alloc(handler, arg, &addr, &data)) < 0)
        return irq;

    uint16_t ctrl = pci_reg16_read(dev, cap + MSI_CTRL);

    pci_reg16_write(dev, cap + MSI_CTRL, ctrl & ~(MSI_CTRL_ENABLE | MSI_CTRL_MME));
    pci_reg32_write(dev, cap + MSI_ADDR, (uint32_t) addr);

    if (ctrl & MSI_CTRL_64BIT) {
        pci_reg32_write(dev, cap + MSI_ADDR + 4, (uint32_t) (addr >> 32));
        pci_reg16_write(dev, cap + MSI_ADDR + 8, data);
    } else {
        pci_reg16_write(dev, cap + MSI_ADDR + 4, data);
    }

    pci_reg16_write(dev, PCI_COMMAND, pci_reg16_read(dev, PCI_COMMAND) | PCI_COMMAND_INTX_OFF);
    pci_reg16_write(dev, cap + MSI_CTRL, (ctrl & ~MSI_CTRL_MME) | MSI_CTRL_ENABLE);

    return irq;
}

MODULE_INIT(pci, pci_prope, NULL)
