#ifndef _DEV_IRQ_H
#define _DEV_IRQ_H

#include <core/types.h>

/*
 * Devices' interrupts. A driver installs a handler for its IRQ, which is
 * called with its `arg` when the interrupt comes. Each architecture
 * implements this (arch/ARCH/.../irq.c).
 *
 * An IRQ is the number that the platform gives it: the ISA's or the I/O
 * APIC's on the PC, the GIC's interrupt ID on the virt machine. A driver
 * gets it from the platform (dev/platform.h), from its bus (a PCI line),
 * or from arch_msi_alloc(); it never makes one up. An IRQ may have
 * several handlers (a shared line): each is called, and each checks its
 * own device. The first handler installed unmasks the IRQ, and the last
 * one uninstalled masks it.
 */

/* An interrupt handler. */
typedef void (*irq_handler_t)(void *arg);

/**
 * Installs a handler for an IRQ. The first handler installed unmasks the
 * IRQ at its interrupt controller.
 *
 * @param irq  the IRQ, the platform's number for it
 * @param fn   the handler
 * @param arg  the argument the handler is called with
 * @return 0, `-EINVAL` if there's no such IRQ, or `-ENOMEM` if there was
 *         no memory for the handler
 */
int  irq_install(unsigned irq, irq_handler_t fn, void *arg);

/**
 * Removes a handler that `irq_install()` installed. The last handler
 * removed masks the IRQ at its interrupt controller.
 *
 * @param irq  the IRQ
 * @param fn   the handler, as it was installed
 * @param arg  its argument, as it was installed
 */
void irq_uninstall(unsigned irq, irq_handler_t fn, void *arg);

#endif /* ! _DEV_IRQ_H */
