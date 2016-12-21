#ifndef _X86_IRQ_H
#define _X86_IRQ_H

#include <core/system.h>

typedef void (*irq_handler_t)(regs_t *);
void irq_install_handler(unsigned irq, irq_handler_t handler);

#define PIT_IRQ	0

#endif /* !_X86_IRQ_H */
