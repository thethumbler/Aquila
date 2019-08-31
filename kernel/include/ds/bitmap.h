#ifndef _DS_BITMAP_H
#define _DS_BITMAP_H

#include <core/system.h>
#include <core/string.h>

/* All operations are uniform regardless of endianess */

/**
 * \ingroup ds
 * \brief bitmap
 */
typedef struct {
    uint32_t *map;
    size_t   max_idx;   /* max index */
} bitmap_t;

/* Number of bits in block */
#define BITMAP_BLOCK_SIZE (32)
/* Block mask */
#define BITMAP_BLOCK_MASK (BITMAP_BLOCK_SIZE - 1)
/* Number of bits in index to encode offset in block */
#define BITMAP_LOG2_BLOCK_SIZE  (5)
/* Offset of the block in bitmap */
#define BITMAP_BLOCK_OFFSET(__bitmap__i__)  ((__bitmap__i__) >> BITMAP_LOG2_BLOCK_SIZE)
/* Offset of bit in block */
#define BITMAP_BIT_OFFSET(__bitmap__i__)    ((__bitmap__i__) & (BITMAP_BLOCK_SIZE - 1))

#define BITMAP_SIZE(n) ((n) + BITMAP_BLOCK_MASK) / BITMAP_BLOCK_SIZE * MEMBER_SIZE(bitmap_t, map[0])
#define BITMAP_NEW(n)  (&(bitmap_t){.map = (uint32_t[BITMAP_SIZE(n)]){0}, .max_idx = (n) - 1})

static inline size_t bitmap_size(size_t n)
{
    return (n + BITMAP_BLOCK_MASK) / BITMAP_BLOCK_SIZE * MEMBER_SIZE(bitmap_t, map[0]);
}

static inline void bitmap_set(bitmap_t *bitmap, size_t index)
{
    bitmap->map[BITMAP_BLOCK_OFFSET(index)] |= 1 << BITMAP_BIT_OFFSET(index);
}

static inline void bitmap_clear(bitmap_t *bitmap, size_t index)
{
    bitmap->map[BITMAP_BLOCK_OFFSET(index)] &= ~(1 << BITMAP_BIT_OFFSET(index));
}

static inline int bitmap_check(bitmap_t *bitmap, size_t index)
{
    return bitmap->map[BITMAP_BLOCK_OFFSET(index)] & (1 << BITMAP_BIT_OFFSET(index));
}

static inline void bitmap_set_range(bitmap_t *bitmap, size_t findex, size_t lindex)
{
    size_t i = findex;

    /* Set non block-aligned indices */
    while (i & BITMAP_BLOCK_MASK && i <= lindex) { 
        bitmap_set(bitmap, i);
        ++i;
    }

    /* Set block-aligned indices */
    if (i < lindex) 
        memset(&bitmap->map[BITMAP_BLOCK_OFFSET(i)], -1, (lindex - i)/BITMAP_BLOCK_SIZE * MEMBER_SIZE(bitmap_t, map[0]));

    i += (lindex - i)/BITMAP_BLOCK_SIZE * BITMAP_BLOCK_SIZE;

    /* Set non block-aligned indices */
    while (i <= lindex) {
        bitmap_set(bitmap, i); ++i;
    }
}

static inline void bitmap_clear_range(bitmap_t *bitmap, size_t findex, size_t lindex)
{
    size_t i = findex;

    /* Set non block-aligned indices */
    while (i & BITMAP_BLOCK_MASK && i <= lindex) { 
        bitmap_clear(bitmap, i);
        ++i;
    }

    /* Set block-aligned indices */
    if (i < lindex) 
        memset(&bitmap->map[BITMAP_BLOCK_OFFSET(i)], 0, (lindex - i)/BITMAP_BLOCK_SIZE * MEMBER_SIZE(bitmap_t, map[0]));

    i += (lindex - i)/BITMAP_BLOCK_SIZE * BITMAP_BLOCK_SIZE;

    /* Set non block-aligned indices */
    while (i <= lindex) {
        bitmap_clear(bitmap, i);
        ++i;
    }
}

#endif /* ! _DS_BITMAP_H */
