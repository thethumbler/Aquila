#ifndef _SYSTEM_H
#define _SYSTEM_H

#define NULL ((void *) 0)
#define _BV(b) (1 << (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define foreach(element, list) \
	for (typeof(*list) *tmp = list, element = *tmp; element; element = *++tmp)

#define forlinked(element, list, iter) \
	for (typeof(list) element = list; element; element = iter)

#define __unused	__attribute__((unused))
#define __packed	__attribute__((packed))
#define __aligned(n) __attribute__((aligned(n)))
#define __section(s) __attribute__((section(s)))

#define MEMBER_SIZE(type, member) (sizeof(((type *)0)->member))

#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <core/printk.h>
#include <config.h>
#include <core/types.h>

#if ARCH==X86
#include <arch/x86/include/system.h>
#endif

static inline int __always(){return 1;}
static inline int __never (){return 0;}

#define __CAT(a, b) a ## b
#define MODULE_INIT(name, i, f) \
    __section(".__minit") void * __CAT(__minit_, name) = i;\
    __section(".__mfini") void * __CAT(__mfini_, name) = f;\


#define __PANIC_MSG "Man the Lifeboats! Women and children first!\n"

#endif /* !_SYSTEM_H */
