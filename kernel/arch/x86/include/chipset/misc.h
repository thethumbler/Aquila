#ifndef _CHIPSET_MISC_H
#define _CHIPSET_MISC_H

#include <cpu/io.h>

typedef void (*__x86_irq_handler_t)();
int  __x86_pic_setup(struct __ioaddr *master, struct __ioaddr *slave);
void __x86_pic_disable();
void __x86_irq_handler_install(unsigned irq, __x86_irq_handler_t handler);
void __x86_irq_handler_uninstall(unsigned irq);

#endif /* ! _CHIPSET_MISC_H */
