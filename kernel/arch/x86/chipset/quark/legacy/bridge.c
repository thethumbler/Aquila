#include <core/system.h>
#include <dev/pci.h>

#include <chipset/root_complex.h>

#define QRK_LEG_BRDG_VENDOR_ID  0x8086
#define QRK_LEG_BRDG_DEVICE_ID  0x095E

struct pci_dev qrk_leg_brdg;

void quark_legacy_bridge_setup()
{
    printk("Quark SoC: Initalizing Legacy Bridge\n");

    if (pci_device_scan(QRK_LEG_BRDG_VENDOR_ID, QRK_LEG_BRDG_DEVICE_ID, &qrk_leg_brdg)) {
        panic("Quark SoC: Legacy Bridge is not found!\n");
    }

    printk("Quark SoC: Legacy Bridge [Bus: %d, Device: %d, Function: %d]\n", qrk_leg_brdg.bus, qrk_leg_brdg.dev, qrk_leg_brdg.func);

    quark_root_complex_init();
}
