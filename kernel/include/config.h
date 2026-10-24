#ifndef _CONFIG_H
#define _CONFIG_H

/*
 * The kernel's build-time settings that C code needs: uname()'s strings,
 * and a few limits. (Which parts are built is the build configuration's,
 * configs/, and reaches C as CONFIG_* macros.) The architecture's own
 * config.h completes this one.
 */

/* Turns a macro's value into a string. */
#define __CONFIG_STR(x)     #x
#define __CONFIG_XSTR(x)    __CONFIG_STR(x)

/* The compiler the kernel was built with. __GNUC__ is tested last,
 * because other compilers define it too, to say they are compatible. */
#if defined(__clang__)
  /* clang's __VERSION__ is a whole description, not just the version */
  #define __CONFIG_COMPILER__             "clang"
  #define __CONFIG_COMPILER_VERSION__     __CONFIG_XSTR(__clang_major__) "." \
                                          __CONFIG_XSTR(__clang_minor__) "." \
                                          __CONFIG_XSTR(__clang_patchlevel__)
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
  #define __CONFIG_COMPILER__             "unknown"
  #define __CONFIG_COMPILER_VERSION__     "?"
#endif

#define UTSNAME_SYSNAME  "AquilaOS"
#define UTSNAME_RELEASE  "v0.0.2"
#define UTSNAME_NODENAME "aquila"
/* when the kernel was built, and with what compiler */
#define UTSNAME_VERSION  __DATE__ " " __TIME__ " " __CONFIG_COMPILER__ " " __CONFIG_COMPILER_VERSION__

#define __CONFIG_TIMESTAMP__            __DATE__ " " __TIME__

/* The most file descriptors a process can have open. */
#define FDS_COUNT   64

/* The size of a process's user stack. */
#define USER_STACK_SIZE (8192 * 1024U)

#endif /* ! _CONFIG_H */
