#ifndef _BUDDY_H
#define _BUDDY_H

#include <core/system.h>
#include <ds/bitmap.h>

struct buddy {
    size_t first_free_idx;
    size_t usable;
    bitmap_t bitmap;
};

#define BUDDY_IDX(idx) ((idx) ^ 0x1)

#endif /* ! _BUDDY_H */
