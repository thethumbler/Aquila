#ifndef _CONFIG_H
#define _CONFIG_H

#define ARCH X86
#define ARCH_X86
#define ARCH_BITS 32
//#define X86_PAE   1
#define MULTIBOOT_GFX   1
//#define EARLYCON_DISABLE_ON_INIT 1
//#define ARCH X86_64
//#define ARCH_X86_64
//#define ARCH_BITS 64

#define FDS_COUNT   64  /* Maximum number of file descriptors per process */
#define USER_STACK_SIZE (8192 * 1024U)  /* 8 MiB */

//#define DEV_FRAMEBUFFER

#define UTSNAME_SYSNAME  "AquilaOS"
#define UTSNAME_RELEASE  "v0.0.1a"
#define UTSNAME_NODENAME "aquila"
#define UTSNAME_VERSION  (__DATE__ " " __TIME__)
#define UTSNAME_MACHINE  "i386"

/* test __GNUC__ last because some compilers report
 * compatability with gcc using __GNUC__
 */
#if defined(__clang__)
  #define __CONFIG_COMPILER__             "clang"
  #define __CONFIG_COMPILER_VERSION__     __VERSION__
#elif defined(__PCC__)
  #define __CONFIG_COMPILER__             "pcc"
  #define __CONFIG_COMPILER_VERSION__     __VERSION__
#elif defined(__TINYC__)
  #define __CONFIG_COMPILER__             "tcc"
  #define __CONFIG_COMPILER_VERSION__     "0.9.27"
#elif defined(__GNUC__)
  #define __CONFIG_COMPILER__             "gcc"
  #define __CONFIG_COMPILER_VERSION__     __VERSION__
#else
  #define __CONFIG_COMPILER__             "unkown"
  #define __CONFIG_COMPILER_VERSION__     "?"
#endif

//#ifdef __TIMESTAMP__
//#define __CONFIG_TIMESTAMP__            __TIMESTAMP__
//#else
#define __CONFIG_TIMESTAMP__            __DATE__ " " __TIME__
//#endif

#endif /* ! _CONFIG_H */
