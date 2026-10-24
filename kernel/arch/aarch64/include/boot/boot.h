#ifndef _AARCH64_BOOT_H
#define _AARCH64_BOOT_H

#include_next <boot/boot.h>

/* Reads the boot loader's devicetree into the kernel's struct boot (the
 * platform's, platform/virt/fdt.c). `dtb` is the devicetree's physical
 * address, which the boot loader passes in x0. */
struct boot *fdt_boot(uintptr_t dtb);

#endif /* ! _AARCH64_BOOT_H */
