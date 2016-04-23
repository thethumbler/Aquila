#ifndef _SYSTEM_H
#define _SYSTEM_H

#define NULL (void*)0
#define _BV(b) (1 << (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define foreach(element, list) \
	for(typeof(*list) *tmp = list, element = *tmp; element; element = *++tmp)

#define forlinked(element, list, iter) \
	for(typeof(list) element = list; element; element = iter)

#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>

#include <core/printk.h>

#define ARCH_X86

#define ARCH X86

#if ARCH==X86
#include <arch/x86/include/system.h>
#endif

#endif /* !_SYSTEM_H */
