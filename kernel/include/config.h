#ifndef _CONFIG_H
#define _CONFIG_H

/* test __GNUC__ last because some compilers report
 * compatability with gcc using __GNUC__
 */
#define __CONFIG_STR(x)     #x
#define __CONFIG_XSTR(x)    __CONFIG_STR(x)

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
/* when and with what the kernel was built */
#define UTSNAME_VERSION  __DATE__ " " __TIME__ " " __CONFIG_COMPILER__ " " __CONFIG_COMPILER_VERSION__

#define __CONFIG_TIMESTAMP__            __DATE__ " " __TIME__

/* Maximum number of file descriptors per process */
#define FDS_COUNT   64

/* Maximum user stack size */
#define USER_STACK_SIZE (8192 * 1024U)

#endif /* ! _CONFIG_H */
