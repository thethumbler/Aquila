#ifndef _MM_BUDDY_H
#define _MM_BUDDY_H

#include <mm/mm.h>

/** max buddy order */
#define BUDDY_MAX_ORDER (10)
/** buddy minimum block size */
#define BUDDY_MIN_BS    (4096)
/** buddy maximum block size */
#define BUDDY_MAX_BS    (BUDDY_MIN_BS << BUDDY_MAX_ORDER)

#define BUDDY_ZONE_NR       2
#define BUDDY_ZONE_DMA      0
#define BUDDY_ZONE_NORMAL   1

int     buddy_setup(size_t total_mem);
paddr_t buddy_alloc(int zone, size_t size);
void    buddy_free(int zone, paddr_t paddr, size_t size);
void    buddy_set_unusable(paddr_t paddr, size_t size);

#endif /* !_MM_BUDDY_H */
