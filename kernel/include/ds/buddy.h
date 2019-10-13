#ifndef _DS_BUDDY_H
#define _DS_BUDDY_H

#include <core/system.h>
#include <ds/bitmap.h>

/**
 * \ingroup ds
 * \brief buddy structure
 */
struct buddy {
    size_t first_free_idx;
    size_t usable;
    struct bitmap bitmap;
};

#define BUDDY_IDX(idx) ((idx) ^ 0x1)

#endif /* ! _DS_BUDDY_H */
