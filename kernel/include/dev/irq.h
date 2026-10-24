#ifndef _DEV_IRQ_H
#define _DEV_IRQ_H

#include <core/types.h>

/*
 * Devices' interrupts, the architecture's (arch/ARCH/.../irq.c): a
 * handler for an IRQ, called with its `arg' when the interrupt comes.
 *
 * An IRQ is the number the platform gives it: the ISA's or the I/O
 * APIC's on the PC, the GIC's interrupt ID on the virt machine. A driver
 * gets it from the platform (dev/platform.h), from its bus (a PCI line),
 * or from arch_msi_alloc(); it never makes one up. An IRQ may have
 * several handlers (a shared line): each is called, each looks at its
 * device. The first unmasks it, the last one uninstalled masks it.
 */

typedef void (*irq_handler_t)(void *arg);

/* 0, or -EINVAL if there's no such IRQ, -ENOMEM no memory for the handler */
int  irq_install(unsigned irq, irq_handler_t fn, void *arg);
void irq_uninstall(unsigned irq, irq_handler_t fn, void *arg);

#endif /* ! _DEV_IRQ_H */
