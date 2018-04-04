#ifndef _CHIPSET_MISC_H
#define _CHIPSET_MISC_H

#include <cpu/io.h>

typedef void (*x86_irq_handler_t)();
int  x86_pic_setup(struct ioaddr *master, struct ioaddr *slave);
void x86_pic_disable();
void x86_irq_handler_install(unsigned irq, x86_irq_handler_t handler);
void x86_irq_handler_uninstall(unsigned irq);

int x86_i8042_setup(struct ioaddr *io);
void x86_i8042_handler_register(int channel, void (*fun)(int));

void x86_pit_setup(struct ioaddr *io);
uint32_t x86_pit_period_set(uint32_t period_ns);

#endif /* ! _CHIPSET_MISC_H */
