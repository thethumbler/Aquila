#include <core/system.h>
#include <core/module.h>
#include <core/arch.h>

#include <dev/pci.h>
#include <cpu/io.h>
#include <mm/mm.h>

/* the platform's host bridge: none, no devices */
static struct pci_host *host;

static inline uint32_t pci_read_dword(uint8_t bus, uint8_t dev, uint8_t func, uint16_t reg)
{
    struct pci_dev d = {.bus = bus, .dev = dev, .func = func};
    return host ? host->read(host, &d, reg) : 0xFFFFFFFF;
}

static inline void pci_write_dword(uint8_t bus, uint8_t dev, uint8_t func, uint16_t reg, uint32_t val)
{
    struct pci_dev d = {.bus = bus, .dev = dev, .func = func};

    if (host)
        host->write(host, &d, reg, val);
}

static inline uint16_t get_vendor_id(uint8_t bus, uint8_t dev, uint8_t func)
{
    return pci_read_dword(bus, dev, func, 0x00) & 0xFFFF;
}

static inline uint16_t get_device_id(uint8_t bus, uint8_t dev, uint8_t func)
{
    return (pci_read_dword(bus, dev, func, 0x00) >> 16) & 0xFFFF;
}

static inline uint8_t get_class_code(uint8_t bus, uint8_t dev, uint8_t func)
{
    return (pci_read_dword(bus, dev, func, 0x08) >> 24) & 0xFF;
}

static inline uint8_t get_subclass_code(uint8_t bus, uint8_t dev, uint8_t func)
{
    return (pci_read_dword(bus, dev, func, 0x08) >> 16) & 0xFF;
}

static inline uint8_t get_header_type(uint8_t bus, uint8_t dev, uint8_t func)
{
    return (pci_read_dword(bus, dev, func, 0x0C) >> 16) & 0xFF;
}

static inline uint32_t get_bar(uint8_t bus, uint8_t dev, uint8_t func, uint8_t id)
{
    return pci_read_dword(bus, dev, func, 0x10 + 4 * id);
}

int pci_device_scan(uint16_t vendor_id, uint16_t device_id, struct pci_dev *ref)
{
    /* TODO cache bus entries */
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

uint8_t pci_reg8_read(struct pci_dev *dev, uint8_t off)
{
    return (pci_read_dword(dev->bus, dev->dev, dev->func, off) >> ((off & 3) * 8)) & 0xFF;
}

uint16_t pci_reg16_read(struct pci_dev *dev, uint8_t off)
{
    return (pci_read_dword(dev->bus, dev->dev, dev->func, off) >> ((off & 2) * 8)) & 0xFFFF;
}

uint32_t pci_reg32_read(struct pci_dev *dev, uint8_t off)
{
    return pci_read_dword(dev->bus, dev->dev, dev->func, off);
}

void pci_reg8_write(struct pci_dev *dev, uint8_t off, uint8_t val)
{
    uint32_t oval = pci_read_dword(dev->bus, dev->dev, dev->func, off);
    oval = (oval & ~(0xFFUL << (off & 3) * 8)) | ((uint32_t) val << (off & 3) * 8);
    pci_write_dword(dev->bus, dev->dev, dev->func, off, oval);
}

void pci_reg16_write(struct pci_dev *dev, uint8_t off, uint16_t val)
{
    uint32_t oval = pci_read_dword(dev->bus, dev->dev, dev->func, off);
    oval = (oval & ~(0xFFFFUL << (off & 2) * 8)) | ((uint32_t) val << (off & 2) * 8);
    pci_write_dword(dev->bus, dev->dev, dev->func, off, oval);
}

void pci_reg32_write(struct pci_dev *dev, uint8_t off, uint32_t val)
{
    pci_write_dword(dev->bus, dev->dev, dev->func, off, val);
}

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

static void scan_bus(uint8_t bus)
{
    for (uint8_t dev = 0; dev < 32; ++dev) {
        scan_device(bus, dev);
    }
}

int pci_prope()
{
    //scan_bus(0);
    return 0;
}

struct dev pcidev = {
    .probe = pci_prope,
};



/*
 * PCI Bus Interface
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
 * BAR `id' of `dev': its address and size (the size is what the device
 * keeps of all ones written to it, decoding off meanwhile), a 64-bit
 * memory BAR takes the next one too. -1 if it decodes nothing.
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
        sized |= 0xFFFFFFFF00000000ULL | 0xFFFF0000; /* 16-bit ports */

    bar->addr = ((uint64_t) hi << 32 | lo) & mask;
    bar->size = ~sized + 1;

    return size_lo ? 0 : -1;
}

/** decoding its memory and ports, and mastering the bus (DMA) */
void pci_enable(struct pci_dev *dev)
{
    uint16_t command = pci_reg16_read(dev, PCI_COMMAND);
    pci_reg16_write(dev, PCI_COMMAND,
        command | PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
}

#define PCI_INTERRUPT_PIN   0x3D

/** the IRQ its interrupt pin comes to: the host's for the pin, swizzled
 * by the device's slot (the bridge's interrupt map: INTA# of slot 1 is
 * INTB#), else the line the firmware wrote; -1 if it has no pin */
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

/** the BARs no firmware assigned (bus 0's devices'), each in its window,
 * aligned to its size (the host's bus addresses) */
static void pci_assign(void)
{
    uint64_t mem = host->mem_base, io = host->io_base;

    for (uint8_t slot = 0; slot < 32; ++slot) {
        for (uint8_t func = 0; func < 8; ++func) {
            struct pci_dev dev = {.bus = 0, .dev = slot, .func = func};

            /* none, or a bridge (its BARs are another layout) */
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

            /* one function only */
            if (!func && !(get_header_type(0, slot, 0) & 0x80))
                break;
        }
    }
}

/**
 * The platform's host bridge (platform_init()): its configuration space
 * mapped (ECAM's memory), the BARs no firmware assigned put in its
 * windows. -1 if it can't be mapped.
 */
int pci_host_set(struct pci_host *h)
{
    if (h->config.type != IOADDR_PORT) {
        void *config = arch_mmio_map(h->config.addr, h->size);

        if (!config)
            return -1;

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

/** the offset of its capability `id' (PCI_CAP_*), 0 if it has none */
uint8_t pci_cap_find(struct pci_dev *dev, uint8_t id)
{
    if (!(pci_reg16_read(dev, PCI_STATUS) & PCI_STATUS_CAP_LIST))
        return 0;

    uint8_t off = pci_reg8_read(dev, PCI_CAPABILITIES) & 0xfc;

    /* at most 48 of them (they're 4 bytes at least, past the header) */
    for (int i = 0; off && i < 48; ++i) {
        if (pci_reg8_read(dev, off) == id)
            return off;

        off = pci_reg8_read(dev, off + 1) & 0xfc;
    }

    return 0;
}

/* MSI's capability: its message control, the message's address (its
 * high half if it's 64-bit), the data after it */
#define MSI_CTRL                2
#define MSI_ADDR                4
#define MSI_CTRL_ENABLE         0x0001
#define MSI_CTRL_MME            0x0070      /* the messages enabled: 1 */
#define MSI_CTRL_64BIT          0x0080

/**
 * The device's interrupts as messages (MSI): one, its IRQ to `handler'
 * (with `arg');
 * its legacy line (INTx) off. The IRQ, -1 if it can't (no capability, or
 * the architecture takes no MSI: the line it is, then).
 */
int pci_msi_enable(struct pci_dev *dev, irq_handler_t handler, void *arg)
{
    uint8_t cap = pci_cap_find(dev, PCI_CAP_MSI);
    uint64_t addr;
    uint32_t data;
    int irq;

    if (!cap || !arch_msi_alloc || (irq = arch_msi_alloc(handler, arg, &addr, &data)) < 0)
        return -1;

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
