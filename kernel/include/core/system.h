#ifndef _SYSTEM_H
#define _SYSTEM_H

#define _BV(b) (1 << (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#if 1
#define foreach(element, list) \
    for (typeof(*list) *tmp = list, element = *tmp; element; element = *++tmp)

#define forlinked(element, list, iter) \
    for (typeof(list) element = list; element; element = iter)
#endif

#define MEMBER_SIZE(type, member) (sizeof(((type *)0)->member))


#define __PANIC_MSG "Bailing out. You are on your own. Good luck.\n"

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

#if defined(__TINYC__)
//  #define __pragma_pack 1
#endif

#if defined(__TINYC__) || defined(__PCC__)
#include <stddef.h>
#else
#include_next <stddef.h>
#include_next <stdint.h>
#endif

//#include <mm/mm.h>
#include <core/printk.h>
#include <mm/kvmem.h>
#include <stdarg.h>
#include <config.h>
#include <core/types.h>

static inline int __always(){return 1;}
static inline int __never (){return 0;}

//#define __PANIC_MSG "Man the Lifeboats! Women and children first!\n"

#include <core/panic.h>
#define assert(x) if(!(x)) panic("Assertion " #x " failed");
#define assert_sizeof(o, sz) if (sizeof(o) != sz) panic("Assertion sizeof(" #o ") == " #sz " failed");
#define assert_alignof(o, al) if (((uintptr_t) o) & (al - 1)) panic("Assertion alignof(" #o ") == " #al " failed");

#endif /* !_SYSTEM_H */
