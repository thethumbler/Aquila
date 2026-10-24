#ifndef _AARCH64_CONFIG_H
#define _AARCH64_CONFIG_H

/* aarch64's build-time settings, completing the kernel's config.h. */

#define ARCH AARCH64
#define ARCH_BITS 64

#define UTSNAME_MACHINE  "aarch64"

/* The machine that the programs it runs are built for (sys/binfmt/elf.c). */
#define ELF_MACHINE      0x00B7   /* EM_AARCH64 */

#include_next <config.h>

#endif /* ! _AARCH64_CONFIG_H */
