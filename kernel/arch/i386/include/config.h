#ifndef _X86_CONFIG_H
#define _X86_CONFIG_H

#if 1
#define ARCH X86
#define ARCH_X86
#define ARCH_BITS 32
#define MULTIBOOT_GFX   1

#define UTSNAME_MACHINE  "i386"
#else
#define ARCH X86_64
#define ARCH_X86_64
#define ARCH_BITS 64
#define MULTIBOOT_GFX   1

#define UTSNAME_MACHINE  "x86_64"
#endif

#include_next <config.h>

#endif /* ! _X86_CONFIG_H */
