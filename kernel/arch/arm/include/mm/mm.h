#ifndef _NONE_MM_MM_H
#define _NONE_MM_MM_H

#define USER_STACK_BASE 0
#define PAGE_SIZE   (0x1000)
#define PAGE_MASK   (PAGE_SIZE - 1)

#define ARCH_KVMEM_BASE 100
#define ARCH_KVMEM_NODES_SIZE   0

#define LMA(n)  (n)
#define VMA(n)  (n)

#include_next <mm/mm.h>

#endif  /* _NONE_MM_MM_H */
