#include <core/system.h>
#include <dev/pci.h>

#define QRK_LEG_BRDG_VENDOR_ID  0x8086
#define QRK_LEG_BRDG_DEVICE_ID  0x095E

struct pci_dev leg_brdg;

void quark_legacy_bridge_setup()
{
    if (pci_device_scan(QRK_LEG_BRDG_VENDOR_ID, QRK_LEG_BRDG_DEVICE_ID, &leg_brdg)) {
        panic("Quark SoC: Legacy Bridge is not found!\n");
    }

    printk("Quark SoC: Legacy Bridge [Bus: %d, Device: %d, Function: %d]\n", leg_brdg.bus, leg_brdg.dev, leg_brdg.func);
    for (;;);
}
