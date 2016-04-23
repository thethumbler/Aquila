#ifndef _BOOT_H
#define _BOOT_H

#include <core/system.h>

typedef struct
{
	void *ptr;
	size_t size;
} module_t;

typedef struct
{
	uintptr_t start;
	uintptr_t end;
} mmap_t;

extern char *kernel_cmdline;
extern module_t *kernel_modules;
extern mmap_t *kernel_mmap;

#endif