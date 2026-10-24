#ifndef _X86_SYSTEM_H
#define _X86_SYSTEM_H

#include <config.h>

/* x86's page size, and the size that one page table maps */
#define PAGE_SIZE   (0x1000)
#define PAGE_MASK   (PAGE_SIZE - 1)
#define PAGE_SHIFT  (12)

#if ARCH_BITS==32
#define TABLE_SIZE  (0x400 * PAGE_SIZE)
#define TABLE_MASK  (TABLE_SIZE - 1)
#else
#define TABLE_SIZE  (0x200 * PAGE_SIZE)
#define TABLE_MASK  (TABLE_SIZE - 1)
#endif

/* where the kernel's half starts, on i386 */
#define KERNEL_BREAK 0xC0000000

#include_next <core/system.h>

#endif /* ! _X86_SYSTEM_H */
