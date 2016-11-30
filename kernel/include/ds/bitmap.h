#ifndef _BITMAP_H
#define _BITMAP_H

#include <core/system.h>
#include <core/string.h>

/* All operations are uniform regardless of endianess */

typedef struct {
	uint32_t *map;
	size_t   max_idx;	/* max index */
} bitmap_t;

/* Number of bits in block */
#define BLOCK_SIZE (32)
/* Block mask */
#define BLOCK_MASK (BLOCK_SIZE - 1)
/* Number of bits in index to encode offset in block */
#define LOG2_BLOCK_SIZE	(5)
/* Offset of the block in bitmap */
#define BLOCK_OFFSET(__bitmap__i__)	((__bitmap__i__) >> LOG2_BLOCK_SIZE)
/* Offset of bit in block */
#define BIT_OFFSET(__bitmap__i__)	((__bitmap__i__) & (BLOCK_SIZE - 1))

static inline size_t bitmap_size(size_t n)
{
	return (n + BLOCK_MASK) / BLOCK_SIZE * MEMBER_SIZE(bitmap_t, map[0]);
}

static inline void bitmap_set(bitmap_t *bitmap, size_t index)
{
	bitmap->map[BLOCK_OFFSET(index)] |= 1 << BIT_OFFSET(index);
}

static inline void bitmap_clear(bitmap_t *bitmap, size_t index)
{
	bitmap->map[BLOCK_OFFSET(index)] &= ~(1 << BIT_OFFSET(index));
}

static inline int bitmap_check(bitmap_t *bitmap, size_t index)
{
	return bitmap->map[BLOCK_OFFSET(index)] & (1 << BIT_OFFSET(index));
}

static inline void bitmap_set_range(bitmap_t *bitmap, size_t findex, size_t lindex)
{
	size_t i = findex;

	/* Set non block-aligned indices */
	while (i & BLOCK_MASK && i <= lindex) { 
		bitmap_set(bitmap, i);
	   	++i;
	}

	/* Set block-aligned indices */
	if (i < lindex) 
		memset(&bitmap->map[BLOCK_OFFSET(i)], -1, (lindex - i)/BLOCK_SIZE * MEMBER_SIZE(bitmap_t, map[0]));

	i += (lindex - i)/BLOCK_SIZE * BLOCK_SIZE;

	/* Set non block-aligned indices */
	while (i <= lindex) {
		bitmap_set(bitmap, i); ++i;
	}
}

static inline void bitmap_set_clear(bitmap_t *bitmap, size_t findex, size_t lindex)
{
	size_t i = findex;

	/* Set non block-aligned indices */
	while (i & BLOCK_MASK && i <= lindex) { 
		bitmap_clear(bitmap, i);
	   	++i;
	}

	/* Set block-aligned indices */
	if (i < lindex) 
		memset(&bitmap->map[BLOCK_OFFSET(i)], 0, (lindex - i)/BLOCK_SIZE * MEMBER_SIZE(bitmap_t, map[0]));

	i += (lindex - i)/BLOCK_SIZE * BLOCK_SIZE;

	/* Set non block-aligned indices */
	while (i <= lindex) {
		bitmap_clear(bitmap, i); ++i;
	}
}

#endif /* !_BITMAP_H */
