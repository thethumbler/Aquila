#ifndef _CONFIG_H
#define _CONFIG_H

#define ARCH X86
#define ARCH_X86
#define ARCH_BITS 32
//#define X86_PAE	1
#define MULTIBOOT_GFX   1
//#define EARLYCON_DISABLE_ON_INIT 1

#define FDS_COUNT	64  /* Maximum number of file descriptors per process */
#define USER_STACK_SIZE	(8192 * 1024U)	/* 8 MiB */

//#define DEV_FRAMEBUFFER

#define UTSNAME_SYSNAME  "AquilaOS"
#define UTSNAME_RELEASE  "v0.0.1a"
#define UTSNAME_NODENAME "aquila"
#define UTSNAME_VERSION  (__DATE__ " " __TIME__)
#define UTSNAME_MACHINE  "i386"

#endif /* ! _CONFIG_H */
