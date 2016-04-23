#ifndef _PANIC_H
#define _PANIC_H

#include <core/system.h>
#include <core/printk.h>

/* FIXME: make halting the system CPU transparent */
#define panic(s) \
{\
	printk("KERNEL PANIC:\n%s [%d] %s: %s\n", \
		__FILE__, __LINE__, __func__, s);\
	asm("cli"); \
	for(;;); \
}\

#endif /* !_PANIC_H */
