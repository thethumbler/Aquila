use crate::include::mm::kvmem::*;
use crate::include::core::types::*;
use crate::{malloc_define, malloc_declare};

malloc_define!(M_RINGBUF, "ring-buffer\0", "ringbuffer structure\0");
malloc_declare!(M_BUFFER);

macro_rules! ring_index {
    ($ring:expr, $i:expr) => {
        (($i) % ((*$ring).size))
    }
}

/*
 * \ingroup ds
 * \brief ring buffer
 */
pub struct RingBuf {
    pub buf: *mut u8,
    pub size: usize,
    pub head: usize,
    pub tail: usize,
}

/*
 * \ingroup ds
 * \brief create a new statically allocated ring buffer
 */
//#define RINGBUF_NEW(sz) (&(struct ringbuf){.buf = (char[sz]){0}, .size = sz, .head = 0, .tail = 0})

/*
 * \ingroup ds
 * \brief create a new dynamically allocated ring buffer
 */
#[no_mangle]
pub unsafe extern "C" fn ringbuf_new(size: usize) -> *mut RingBuf {
    let ring = kmalloc(core::mem::size_of::<RingBuf>(), &M_RINGBUF, M_ZERO) as *mut RingBuf;

    if ring.is_null() {
        return core::ptr::null_mut();
    }

    (*ring).buf = kmalloc(size, &M_BUFFER, 0);

    if (*ring).buf.is_null() {
        kfree(ring as *mut u8);
        return core::ptr::null_mut();
    }

    (*ring).size = size;
    (*ring).head = 0;
    (*ring).tail = 0;

    return ring;
}

/*
 * \ingroup ds
 * \brief free a dynamically allocated ring buffer
 */
#[no_mangle]
pub unsafe extern "C" fn ringbuf_free(r: *mut RingBuf) -> () {
    if r.is_null() {
        return;
    }

    kfree((*r).buf as *mut u8);
    kfree(r as *mut u8);
}

/* 
 * \ingroup ds
 * \brief read from a ring buffer
 */
#[no_mangle]
pub unsafe extern "C" fn ringbuf_read(ring: *mut RingBuf, n: usize, buf: *mut u8) -> usize {
    let size = n;
    let mut n = n;
    let mut buf = buf;

    while n > 0 {
        if (*ring).head == (*ring).tail {   /* Ring is empty */
            break;
        }

        if (*ring).head == (*ring).size {
            (*ring).head = 0;
        }

        *buf = *(*ring).buf.offset((*ring).head as isize);
        (*ring).head += 1;
        buf  = buf.offset(1);
        n -= 1;
    }

    return size - n;
}

/* 
 * \ingroup ds
 * \brief peek (non-destructive read) from a ring buffer
 */
#[no_mangle]
pub unsafe extern "C" fn ringbuf_read_noconsume(ring: *mut RingBuf, off: off_t, n: usize, buf: *mut u8) -> usize {
    let size = n;

    let mut head = (*ring).head + off as usize;
    let mut n = n;
    let mut buf = buf;

    if (*ring).head < (*ring).tail && head > (*ring).tail {
        return 0;
    }

    while n > 0 {
        if head == (*ring).size {
            head = 0;
        }

        if head == (*ring).tail { /* Ring is empty */
            break;
        }

        *buf = *(*ring).buf.offset(head as isize);
        head += 1;
        buf = buf.offset(1);
        n -= 1;
    }

    return size - n;
}

#[no_mangle]
pub unsafe extern "C" fn ringbuf_write(ring: *mut RingBuf, n: usize, buf: *mut u8) -> usize {
    let size = n;

    let mut n = n;
    let mut buf = buf;

    while n > 0 {
        if ring_index!(ring, (*ring).head) == ring_index!(ring, (*ring).tail) + 1 { /* Ring is full */
            break;
        }

        if (*ring).tail == (*ring).size {
            (*ring).tail = 0;
        }
        
        *(*ring).buf.offset((*ring).tail as isize) = *buf;
        (*ring).tail += 1;
        buf = buf.offset(1);
        n -= 1;
    }

    return size - n;
}

#[no_mangle]
pub unsafe extern "C" fn ringbuf_write_overwrite(ring: *mut RingBuf, n: usize, buf: *mut u8) -> usize {
    let size = n;

    let mut n = n;
    let mut buf = buf;

    while n > 0 {
        if ring_index!(ring, (*ring).head) == ring_index!(ring, (*ring).tail) + 1 {
            /* move head to match */
            (*ring).head = ring_index!(ring, (*ring).head) + 1;
        }

        if (*ring).tail == (*ring).size {
            (*ring).tail = 0;
        }
        
        *(*ring).buf.offset((*ring).tail as isize) = *buf;
        (*ring).tail += 1;
        buf = buf.offset(1);
        n -= 1;
    }

    return size - n;
}

#[no_mangle]
pub unsafe extern "C" fn ringbuf_available(ring: *mut RingBuf) -> usize {
    if (*ring).tail >= (*ring).head {
        return (*ring).tail - (*ring).head;
    }

    return (*ring).tail + (*ring).size - (*ring).head;
}
