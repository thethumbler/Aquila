#ifndef _DS_BITMAP_H
#define _DS_BITMAP_H

#include <core/system.h>
#include <core/string.h>

/*
 * Bitmaps: one bit per thing (a page, a block), set or clear. The bits
 * are stored in 32-bit blocks; bit i is bit (i % 32) of block (i / 32),
 * whatever the CPU's byte order.
 */

/* A block of a bitmap's bits. */
typedef uint32_t bitmap_t;

/** A bitmap: its blocks, and its last bit's index. */
struct bitmap {
    bitmap_t *map;
    size_t   max_idx;   /* the index of its last bit */
};

/* The number of bits in a block, and the mask of a bit's place in it. */
#define BITMAP_BLOCK_SIZE (32)
#define BITMAP_BLOCK_MASK (BITMAP_BLOCK_SIZE - 1)
/* log2 of the block size */
#define BITMAP_LOG2_BLOCK_SIZE  (5)
/* The block that bit i is in, and its place in that block. */
#define BITMAP_BLOCK_OFFSET(__bitmap__i__)  ((__bitmap__i__) >> BITMAP_LOG2_BLOCK_SIZE)
#define BITMAP_BIT_OFFSET(__bitmap__i__)    ((__bitmap__i__) & (BITMAP_BLOCK_SIZE - 1))

/* The size in bytes of a bitmap of n bits, and a new static one, clear. */
#define BITMAP_SIZE(n) ((n) + BITMAP_BLOCK_MASK) / BITMAP_BLOCK_SIZE * MEMBER_SIZE(struct bitmap, map[0])
#define BITMAP_NEW(n)  (&(struct bitmap){.map = (bitmap_t[BITMAP_SIZE(n)]){0}, .max_idx = (n) - 1})

/** Returns the size in bytes of a bitmap of `n` bits. */
static inline size_t bitmap_size(size_t n)
{
    return (n + BITMAP_BLOCK_MASK) / BITMAP_BLOCK_SIZE * MEMBER_SIZE(struct bitmap, map[0]);
}

/** Sets a bit. */
static inline void bitmap_set(struct bitmap *bitmap, size_t index)
{
    bitmap->map[BITMAP_BLOCK_OFFSET(index)] |= 1U << BITMAP_BIT_OFFSET(index);
}

/** Clears a bit. */
static inline void bitmap_clear(struct bitmap *bitmap, size_t index)
{
    bitmap->map[BITMAP_BLOCK_OFFSET(index)] &= ~(1U << BITMAP_BIT_OFFSET(index));
}

/** Returns whether a bit is set (non-zero if it is). */
static inline int bitmap_check(struct bitmap *bitmap, size_t index)
{
    return bitmap->map[BITMAP_BLOCK_OFFSET(index)] & (1U << BITMAP_BIT_OFFSET(index));
}

/** Sets the bits from `findex` to `lindex`, inclusive. */
static inline void bitmap_set_range(struct bitmap *bitmap, size_t findex, size_t lindex)
{
    if (findex > lindex)
        return;

    size_t i = findex;

    /* the bits before the first whole block, one by one */
    while (i & BITMAP_BLOCK_MASK && i <= lindex) { 
        bitmap_set(bitmap, i);
        ++i;
    }

    if (i > lindex)
        return;

    /* the whole blocks, all at once */
    if (i < lindex) 
        memset(&bitmap->map[BITMAP_BLOCK_OFFSET(i)], -1, (lindex - i)/BITMAP_BLOCK_SIZE * MEMBER_SIZE(struct bitmap, map[0]));

    i += (lindex - i)/BITMAP_BLOCK_SIZE * BITMAP_BLOCK_SIZE;

    /* the bits after the last whole block, one by one */
    while (i <= lindex) {
        bitmap_set(bitmap, i); ++i;
    }
}

/** Clears the bits from `findex` to `lindex`, inclusive. */
static inline void bitmap_clear_range(struct bitmap *bitmap, size_t findex, size_t lindex)
{
    if (findex > lindex)
        return;

    size_t i = findex;

    /* the bits before the first whole block, one by one */
    while (i & BITMAP_BLOCK_MASK && i <= lindex) { 
        bitmap_clear(bitmap, i);
        ++i;
    }

    if (i > lindex)
        return;

    /* the whole blocks, all at once */
    if (i < lindex) 
        memset(&bitmap->map[BITMAP_BLOCK_OFFSET(i)], 0, (lindex - i)/BITMAP_BLOCK_SIZE * MEMBER_SIZE(struct bitmap, map[0]));

    i += (lindex - i)/BITMAP_BLOCK_SIZE * BITMAP_BLOCK_SIZE;

    /* the bits after the last whole block, one by one */
    while (i <= lindex) {
        bitmap_clear(bitmap, i);
        ++i;
    }
}

#endif /* ! _DS_BITMAP_H */
