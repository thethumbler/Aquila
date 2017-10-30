#include <dev/pci.h>
#include <dev/pci_list.h>
#include <cpu/io.h>

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

uint32_t pci_read_dword(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg)
{
    union pci_address adr = {0};

    adr.structure.enable = 1;
    adr.structure.bus = bus;
    adr.structure.device = dev;
    adr.structure.function = func;
    adr.structure.reg = reg & 0xfc;

    outl(PCI_CONFIG_ADDRESS, adr.raw);
    return inl(PCI_CONFIG_DATA);
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
    printk("pci_prope()\n");

    scan_bus(0);
    return 0;

    //for (;;);
}

dev_t pcidev = {
    .probe = pci_prope,
};
