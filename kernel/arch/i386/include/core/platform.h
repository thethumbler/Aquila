#ifndef _X86_CORE_PLATFORM_H
#define _X86_CORE_PLATFORM_H

int platform_init(void);
void platform_timer_setup(size_t period_ns, void (*handler)());

#endif /* ! _X86_CORE_PLATFORM_H */
