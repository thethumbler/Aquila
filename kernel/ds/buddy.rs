use crate::ds::bitmap::*;

/**
 * \ingroup ds
 * \brief buddy structure
 */
#[repr(C)]
#[derive(Copy, Clone)]
pub struct Buddy {
    pub first_free_idx: usize,
    pub usable: usize,
    pub bitmap: BitMap,
}

#[macro_export]
macro_rules! buddy_idx {
    ($idx:expr) => {
        (($idx) ^ 0x1)
    }
}
