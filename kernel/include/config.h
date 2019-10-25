#ifndef _CONFIG_H
#define _CONFIG_H

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

#define UTSNAME_SYSNAME  "AquilaOS"
#define UTSNAME_RELEASE  "v0.0.2"
#define UTSNAME_NODENAME "aquila"
#define UTSNAME_VERSION  (__DATE__ " " __TIME__)

#define __CONFIG_TIMESTAMP__            __DATE__ " " __TIME__

/* Maximum number of file descriptors per process */
#define FDS_COUNT   64

/* Maximum user stack size */
#define USER_STACK_SIZE (8192 * 1024U)

#endif /* ! _CONFIG_H */
