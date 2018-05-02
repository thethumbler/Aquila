#ifndef _MM_BUDDY_H
#define _MM_BUDDY_H

#define BUDDY_MAX_ORDER (10)
#define BUDDY_MIN_BS    (4096)
#define BUDDY_MAX_BS    (BUDDY_MIN_BS << BUDDY_MAX_ORDER)

#define BUDDY_ZONE_NR       2
#define BUDDY_ZONE_DMA      0
#define BUDDY_ZONE_NORMAL   1

void buddy_setup(size_t);
uintptr_t buddy_alloc(int, size_t);
void buddy_free(int, uintptr_t, size_t);
void buddy_set_unusable(uintptr_t, size_t);

//void buddy_dump();

#endif /* !_MM_BUDDY_H */
