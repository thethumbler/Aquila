#include <dev/pci.h>
#include <dev/pci_list.h>
#include <cpu/io.h>

static struct ioaddr __pci_ioaddr;

static inline const char *get_vendor_name(uint16_t vendor_id)
{
    for (size_t i = 0; i < PCI_VENTABLE_LEN; ++i)
        if (PciVenTable[i].VenId == vendor_id)
            return PciVenTable[i].VenFull;

    return NULL;
}

static inline const char *get_device_name(uint16_t vendor_id, uint16_t device_id)
{
    for (size_t i = 0; i < PCI_DEVTABLE_LEN; ++i)
        if (PciDevTable[i].VenId == vendor_id && PciDevTable[i].DevId == device_id)
            return PciDevTable[i].ChipDesc;

    return NULL;
}

static inline uint32_t pci_read_dword(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg)
{
    union pci_address adr = {0};

    adr.structure.enable = 1;
    adr.structure.bus = bus;
    adr.structure.device = dev;
    adr.structure.function = func;
    adr.structure.reg = reg & 0xfc;

    io_out32(&__pci_ioaddr, PCI_CONFIG_ADDRESS, adr.raw);
    return io_in32(&__pci_ioaddr, PCI_CONFIG_DATA);
}

static inline void pci_write_dword(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint32_t val)
{
    union pci_address adr = {0};

    adr.structure.enable = 1;
    adr.structure.bus = bus;
    adr.structure.device = dev;
    adr.structure.function = func;
    adr.structure.reg = reg & 0xfc;

    io_out32(&__pci_ioaddr, PCI_CONFIG_ADDRESS, adr.raw);
    io_out32(&__pci_ioaddr, PCI_CONFIG_DATA, val);
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
    oval = (oval & (~0xFFUL << (off & 3) * 8)) | (val << (off & 3) * 8);
    pci_write_dword(dev->bus, dev->dev, dev->func, off, oval);
}

void pci_reg16_write(struct pci_dev *dev, uint8_t off, uint8_t val)
{
    uint32_t oval = pci_read_dword(dev->bus, dev->dev, dev->func, off);
    oval = (oval & (~0xFFFFUL << (off & 2) * 8)) | (val << (off & 2) * 8);
    pci_write_dword(dev->bus, dev->dev, dev->func, off, oval);
}

void pci_reg32_write(struct pci_dev *dev, uint8_t off, uint8_t val)
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
            printk("  -> Vednor: %s\n", get_vendor_name(vendor_id));
            printk("  -> Device: %s\n", get_device_name(vendor_id, device_id));
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
 * Used by chipset driver to initalize PCI bus
 */
void pci_ioaddr_set(struct ioaddr *io)
{
    __pci_ioaddr = *io;
}


/*
 * PCI Bus Interface
 */

int pci_scan_device(uint8_t class, uint8_t subclass, struct pci_dev *_dev)
{
    uint8_t bus = 0;
    for (uint8_t dev = 0; dev < 32; ++dev) {
        for (uint8_t func = 0; func < 8; ++func) {
            uint16_t vendor_id = get_vendor_id(bus, dev, func);
            if (vendor_id != 0xFFFF) {
                //uint32_t device_id = get_device_id(bus, dev, func);
                uint32_t class_code = get_class_code(bus, dev, func);
                uint32_t subclass_code = get_subclass_code(bus, dev, func);
                if (class_code == class && subclass_code == subclass) {
                    _dev->bus = bus;
                    _dev->dev = dev;
                    _dev->func = func;
                    return 0;
                }
            }
        }
    }

    return -1;
}

MODULE_INIT(pci, pci_prope, NULL)
