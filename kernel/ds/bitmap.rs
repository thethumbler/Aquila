/* All operations are uniform regardless of endianess */
pub type bitmap_t = u32;

/**
 * \ingroup ds
 * \brief bitmap
 */
#[repr(C)]
#[derive(Copy, Clone)]
pub struct BitMap {
    pub map: *mut bitmap_t,
    pub max_idx: usize,   /* max index */
}

/* Number of bits in block */
const BITMAP_BLOCK_SIZE: usize = 32;
/* Block mask */
const BITMAP_BLOCK_MASK: usize = (BITMAP_BLOCK_SIZE - 1);
/* Number of bits in index to encode offset in block */
const BITMAP_LOG2_BLOCK_SIZE: usize = 5;
/* Offset of the block in bitmap */
macro_rules! bitmap_block_offset {
    ($i:expr) => {
        (($i) >> BITMAP_LOG2_BLOCK_SIZE)
    }
}

/* Offset of bit in block */
macro_rules! bitmap_bit_offset {
    ($i:expr) => {
        (($i) & (BITMAP_BLOCK_SIZE - 1))
    }
}

macro_rules! bitmap_size {
    ($n:expr) => {
        (($n) + BITMAP_BLOCK_MASK) / BITMAP_BLOCK_SIZE * core::mem::size_of::<bitmap_t>()
    }
}

//#define BITMAP_NEW(n)  (&(struct bitmap){.map = (bitmap_t[BITMAP_SIZE(n)]){0}, .max_idx = (n) - 1})

#[no_mangle]
pub unsafe extern "C" fn bitmap_size(n: usize) -> usize {
    return (n + BITMAP_BLOCK_MASK) / BITMAP_BLOCK_SIZE * core::mem::size_of::<bitmap_t>();
}

#[no_mangle]
pub unsafe extern "C" fn bitmap_set(bitmap: *mut BitMap, index: usize) -> () {
    *(*bitmap).map.offset(bitmap_block_offset!(index) as isize) |= 1 << bitmap_bit_offset!(index);
}

#[no_mangle]
pub unsafe extern "C" fn bitmap_clear(bitmap: *mut BitMap, index: usize) -> () {
    *(*bitmap).map.offset(bitmap_block_offset!(index) as isize) &= !(1 << bitmap_bit_offset!(index));
}

#[no_mangle]
pub unsafe extern "C" fn bitmap_check(bitmap: *mut BitMap, index: usize) -> isize {
    return (*(*bitmap).map.offset(bitmap_block_offset!(index) as isize) & (1 << bitmap_bit_offset!(index))) as isize;
}

#[no_mangle]
pub unsafe extern "C" fn bitmap_set_range(bitmap: *mut BitMap, findex: usize, lindex: usize) -> () {
    let mut i = findex;

    /* Set non block-aligned indices */
    while (i & BITMAP_BLOCK_MASK) != 0 && i <= lindex { 
        bitmap_set(bitmap, i);
        i += 1;
    }

    /* Set block-aligned indices */
    if i < lindex {
        //memset(&bitmap->map[BITMAP_BLOCK_OFFSET(i)], -1, (lindex - i)/BITMAP_BLOCK_SIZE * MEMBER_SIZE(struct bitmap, map[0]));
        core::ptr::write_bytes((*bitmap).map.offset(bitmap_block_offset!(i) as isize), !0u8, (lindex - i)/BITMAP_BLOCK_SIZE * core::mem::size_of::<bitmap_t>());
    }

    i += (lindex - i)/BITMAP_BLOCK_SIZE * BITMAP_BLOCK_SIZE;

    /* Set non block-aligned indices */
    while i <= lindex {
        bitmap_set(bitmap, i);
        i += 1;
    }
}

#[no_mangle]
pub unsafe extern "C" fn bitmap_clear_range(bitmap: *mut BitMap, findex: usize, lindex: usize) -> () {
    let mut i = findex;

    /* Set non block-aligned indices */
    while (i & BITMAP_BLOCK_MASK) != 0 && i <= lindex { 
        bitmap_clear(bitmap, i);
        i += 1;
    }

    /* Set block-aligned indices */
    if (i < lindex) {
        core::ptr::write_bytes((*bitmap).map.offset(bitmap_block_offset!(i) as isize), 0, (lindex - i)/BITMAP_BLOCK_SIZE * core::mem::size_of::<bitmap_t>());
    }

    i += (lindex - i)/BITMAP_BLOCK_SIZE * BITMAP_BLOCK_SIZE;

    /* Set non block-aligned indices */
    while (i <= lindex) {
        bitmap_clear(bitmap, i);
        i += 1;
    }
}
