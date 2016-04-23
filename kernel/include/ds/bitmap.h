#ifndef _BITMAP_H
#define _BITMAP_H

#include <core/system.h>
#include <core/string.h>

/* All operations are done with most significant bit position ignorance */

typedef uint32_t* bitmap_t;
#define BLOCK_SIZE (32)
#define LOG2_BLOCK_SIZE	(5)
#define BLOCK_OFFSET(i)	((i) >> LOG2_BLOCK_SIZE)
#define BLOCK_MASK(i)	((i) & (BLOCK_SIZE - 1))

#define BITMAP_SIZE(n) \
	((n + 31) / BLOCK_SIZE * sizeof(uint32_t))

#define BITMAP_SET(bitmap, index) \
	bitmap[BLOCK_OFFSET(index)] |= _BV(BLOCK_MASK(index))

#define BITMAP_CLR(bitmap, index) \
	bitmap[BLOCK_OFFSET(index)] &= ~_BV(BLOCK_MASK(index))

#define BITMAP_CHK(bitmap, index) \
	(bitmap[BLOCK_OFFSET(index)] & _BV(BLOCK_MASK(index)) ? 1 : 0)

#define BITMAP_SET_RANGE(bitmap, findex, lindex) \
{\
	uint32_t i = (findex); \
	while(i - (findex) < BLOCK_SIZE - 1 && i <= (lindex)) \
	{BITMAP_SET(bitmap, i); ++i;} \
	if(i < (lindex)) \
		memset(&bitmap[BLOCK_OFFSET(i)], -1, \
			((lindex) - i)/BLOCK_SIZE * sizeof(*bitmap)); \
	i += ((lindex) - i)/BLOCK_SIZE * BLOCK_SIZE; \
	while(i <= (lindex)) {BITMAP_SET(bitmap, i); ++i;} \
}\

#define BITMAP_CLR_RANGE(bitmap, findex, lindex) \
{\
	uint32_t i = (findex); \
	while(i - (findex) < BLOCK_SIZE - 1 && i <= (lindex)) \
	{BITMAP_CLR(bitmap, i); ++i;} \
	if(i < (lindex)) \
		memset(&bitmap[BLOCK_OFFSET(i)], 0, \
			((lindex) - i)/BLOCK_SIZE * sizeof(*bitmap)); \
	i += ((lindex) - i)/BLOCK_SIZE * BLOCK_SIZE; \
	while(i <= (lindex)) {BITMAP_CLR(bitmap, i); ++i;} \
}\

#endif /* !_BITMAP_H */
