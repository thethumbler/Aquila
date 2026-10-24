#ifndef _X86_PIT_H
#define _X86_PIT_H

#include <core/system.h>

/* An old declaration: the PIT is set up by x86_pit_setup() now
 * (platform/misc.h), and nothing includes this file. */
void pit_setup(uint32_t freq);

#endif /* ! _X86_PIT_H */
