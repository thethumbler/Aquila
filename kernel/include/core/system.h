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

#define MEMBER_SIZE(type, member) (sizeof(((type *)0)->member))

#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>

/* FIXME -- should be moved elsewhere */
typedef int pid_t;
typedef long int off_t;
typedef long int ssize_t;

#include <core/printk.h>

#include <config.h>

#if ARCH==X86
#include <arch/x86/include/system.h>
#endif

static inline int __always(){return 1;}
static inline int __never (){return 0;}

#endif /* !_SYSTEM_H */
