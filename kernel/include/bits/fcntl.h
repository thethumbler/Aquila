#ifndef _FCNTL_H
#define _FCNTL_H

#include <core/system.h>

#if ARCH == X86
	#include <arch/x86/include/bits/fcntl.h>
#endif

#define O_RDONLY  00
#define O_WRONLY  01
#define O_RDWR    02

#endif