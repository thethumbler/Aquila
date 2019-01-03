#ifndef _CPU_SYS_H
#define _CPU_SYS_H

void x86_lgdt(void *);
void x86_lidt(uintptr_t);
void x86_ltr(uintptr_t);

#endif /* ! _CPU_SYS_H */
