#ifndef _CORE_CHIPSET_H
#define _CORE_CHIPSET_H

int chipset_init();
void chipset_timer_setup(size_t period_ns, void (*handler)());

#endif /* ! _CORE_CHIPSET_H */
