#ifndef _CORE_SYSTEM_H
#define _CORE_SYSTEM_H

#define _BV(b) (1 << (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define MEMBER_SIZE(type, member) (sizeof(((type *)0)->member))

#if defined(__TINYC__) || defined(__clang__) || defined(__PCC__)
  #undef __unused
  #define __unused
#else
  #ifndef __unused
    #define __unused    __attribute__((unused))
  #endif
#endif


#define __packed     __attribute__((packed))
#define __aligned(n) __attribute__((aligned(n)))
#define __section(s) __attribute__((section(s)))

#if defined(__TINYC__) || defined(__PCC__)
#include <stddef.h>
#else
#include_next <stddef.h>
#include_next <stdint.h>
#endif

#include <core/printk.h>
#include <mm/kvmem.h>

#include <config.h>
#include <core/types.h>

#endif /* ! _CORE_SYSTEM_H */
