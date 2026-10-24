#ifndef _X86_CONFIG_H
#define _X86_CONFIG_H

/* x86: what the kernel needs to know of the architecture, i386 or x86_64
 * (the generic part is include/config.h). */

#ifndef __x86_64__
#define ARCH X86
#define ARCH_X86
#define ARCH_BITS 32
#define MULTIBOOT_GFX   1

/* the machine of the programs it runs (sys/binfmt/elf.c) */
#define ELF_MACHINE      0x0003   /* EM_386 */

#define UTSNAME_MACHINE  "i386"
#else
#define ARCH X86_64
#define ARCH_X86_64
#define ARCH_BITS 64
#define MULTIBOOT_GFX   1

/* the machine of the programs it runs (sys/binfmt/elf.c) */
#define ELF_MACHINE      0x003E   /* EM_X86_64 */

#define UTSNAME_MACHINE  "x86_64"
#endif

#include_next <config.h>

#endif /* ! _X86_CONFIG_H */
