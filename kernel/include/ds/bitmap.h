#ifndef _BITMAP_H
#define _BITMAP_H

#include <core/system.h>
#include <core/string.h>

/* All operations are done with most significant bit position ignorance */

typedef uint32_t* bitmap_t;
#define BLOCK_SIZE (32)
#define LOG2_BLOCK_SIZE	(5)
#define BLOCK_OFFSET(__bitmap__i__)	((__bitmap__i__) >> LOG2_BLOCK_SIZE)
#define BLOCK_MASK(__bitmap__i__)	((__bitmap__i__) & (BLOCK_SIZE - 1))

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
	uint32_t __bitmap__i__ = (findex); \
	while(__bitmap__i__ - (findex) < BLOCK_SIZE - 1 && __bitmap__i__ <= (lindex)) \
	{BITMAP_SET(bitmap, __bitmap__i__); ++__bitmap__i__;} \
	if(__bitmap__i__ < (lindex)) \
		memset(&bitmap[BLOCK_OFFSET(__bitmap__i__)], -1, \
			((lindex) - __bitmap__i__)/BLOCK_SIZE * sizeof(*bitmap)); \
	__bitmap__i__ += ((lindex) - __bitmap__i__)/BLOCK_SIZE * BLOCK_SIZE; \
	while(__bitmap__i__ <= (lindex)) {BITMAP_SET(bitmap, __bitmap__i__); ++__bitmap__i__;} \
}\

#define BITMAP_CLR_RANGE(bitmap, findex, lindex) \
{\
	uint32_t __bitmap__i__ = (findex); \
	while(__bitmap__i__ - (findex) < BLOCK_SIZE - 1 && __bitmap__i__ <= (lindex)) \
	{BITMAP_CLR(bitmap, __bitmap__i__); ++__bitmap__i__;} \
	if(__bitmap__i__ < (lindex)) \
		memset(&bitmap[BLOCK_OFFSET(__bitmap__i__)], 0, \
			((lindex) - __bitmap__i__)/BLOCK_SIZE * sizeof(*bitmap)); \
	__bitmap__i__ += ((lindex) - __bitmap__i__)/BLOCK_SIZE * BLOCK_SIZE; \
	while(__bitmap__i__ <= (lindex)) {BITMAP_CLR(bitmap, __bitmap__i__); ++__bitmap__i__;} \
}\

#endif /* !_BITMAP_H */
