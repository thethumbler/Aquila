#ifndef _CPU_SYS_H
#define _CPU_SYS_H

#include <core/system.h>

/* Load the GDT, the IDT and the task register (cpu.S). */
void x86_lgdt(uint16_t, uintptr_t);
void x86_lidt(uintptr_t);
void x86_ltr(uintptr_t);

#endif /* ! _CPU_SYS_H */
