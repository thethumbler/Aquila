#ifndef _X86_CORE_PLATFORM_H
#define _X86_CORE_PLATFORM_H

#include <dev/irq.h>

int platform_init(void);
uint32_t platform_timer_setup(size_t period_ns, irq_handler_t handler);

#endif /* ! _X86_CORE_PLATFORM_H */
