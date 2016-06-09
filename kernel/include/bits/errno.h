#ifndef _ERRNO_H
#define _ERRNO_H

#include <core/system.h>

#if ARCH == X86
	#include <arch/x86/include/bits/errno.h>
#endif

#endif