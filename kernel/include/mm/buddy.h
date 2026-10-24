#ifndef _MM_BUDDY_H
#define _MM_BUDDY_H

#include <mm/mm.h>

/* The buddy allocator (mm/buddy.c): physical memory, in blocks of 2^n
 * pages. */

/* The largest order, and the smallest and largest block sizes. */
#define BUDDY_MAX_ORDER (10)
#define BUDDY_MIN_BS    (4096)
#define BUDDY_MAX_BS    (BUDDY_MIN_BS << BUDDY_MAX_ORDER)

/* The zones of RAM: below 16 MiB (for ISA DMA), and above. */
#define BUDDY_ZONE_NR       2
#define BUDDY_ZONE_DMA      0
#define BUDDY_ZONE_NORMAL   1

/* Where each zone starts. */
extern uintptr_t buddy_zone_offset[];

/* Sets up the allocator; allocates and frees blocks; marks memory used at
 * boot. */
int     buddy_setup(size_t total_mem);
paddr_t buddy_alloc(int zone, size_t size);
void    buddy_free(int zone, paddr_t paddr, size_t size);
void    buddy_set_unusable(paddr_t paddr, size_t size);

#endif /* !_MM_BUDDY_H */
