#ifndef _X86_CORE_PLATFORM_H
#define _X86_CORE_PLATFORM_H

#include <dev/irq.h>

/**
 * Sets up the platform at boot: its interrupt controllers, timers, PCI
 * host bridge and other devices, then the other CPUs. Each platform
 * implements it; the architecture calls it before `kmain()`.
 *
 * @return 0
 */
int platform_init(void);

/* Starts the tick, every `period_ns`; returns the period it really is. */
uint32_t platform_timer_setup(size_t period_ns, irq_handler_t handler);

#endif /* ! _X86_CORE_PLATFORM_H */
