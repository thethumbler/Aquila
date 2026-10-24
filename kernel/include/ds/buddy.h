#ifndef _DS_BUDDY_H
#define _DS_BUDDY_H

#include <core/system.h>
#include <ds/bitmap.h>

/** One order of a buddy allocator's zone (mm/buddy.c): a bitmap of its
 * blocks, a set bit for a block that isn't free. */
struct buddy {
    size_t first_free_idx;  /* where to start looking for a free block */
    size_t usable;          /* how many of its blocks are free */
    struct bitmap bitmap;
};

/* The index of a block's buddy: the other half of the block they split
 * from. */
#define BUDDY_IDX(idx) ((idx) ^ 0x1)

#endif /* ! _DS_BUDDY_H */
