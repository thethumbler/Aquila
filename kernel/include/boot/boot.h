#ifndef _BOOT_H
#define _BOOT_H

#include <core/system.h>

typedef struct
{
	void *addr;
	size_t size;
	char *cmdline;
} module_t;

typedef struct
{
	uintptr_t start;
	uintptr_t end;
} mmap_t;

extern char *kernel_cmdline;
extern int kernel_modules_count;
extern uintptr_t kernel_total_mem;
extern int kernel_mmap_count;
extern module_t *kernel_modules;
extern mmap_t *kernel_mmap;

#endif