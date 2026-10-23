#ifndef _AARCH64_BOOT_H
#define _AARCH64_BOOT_H

#include_next <boot/boot.h>

/* platform: the boot loader's devicetree (its physical address, x0 at
 * the entry) to the kernel's struct boot */
struct boot *fdt_boot(uintptr_t dtb);

#endif /* ! _AARCH64_BOOT_H */
