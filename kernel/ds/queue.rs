use core::marker::Sync;
use crate::include::mm::kvmem::*;
use crate::malloc_define;

use crate::alloc::Box;

malloc_define!(M_QUEUE, "queue\0", "queue structure\0");
malloc_define!(M_QNODE, "queue-node\0", "queue node structure\0");

/*
 * \ingroup ds
 * \brief queue node
 */
pub struct Qnode {
    pub value: *mut u8,
    pub prev: *mut Qnode,
    pub next: *mut Qnode,
}

unsafe impl Sync for Qnode {}

/*
 * \ingroup ds
 * \brief queue
 */
pub struct Queue {
    pub count: usize,
    pub head: *mut Qnode,
    pub tail: *mut Qnode,
    pub flags: usize,
}

unsafe impl Sync for Queue {}
//unsafe impl Sync for *mut Queue {}

impl Default for Queue {
    fn default() -> Self {
        Self {
            count: 0,
            head: core::ptr::null_mut(),
            tail: core::ptr::null_mut(),
            flags: 0,
        }
    }
}

pub struct QueueIterator<'a> {
    cur: &'a Qnode,
}

impl<'a> Iterator for QueueIterator<'a> {
    type Item = &'a Qnode;

    fn next(&mut self) -> Option<Self::Item> {
        unsafe {
            let node = self.cur as *const _ as *mut Qnode;

            if node.is_null() {
                return None
            }

            self.cur = &*(*node).next;

            Some(&*node)
        }
    }
}

/*
 * \ingroup ds
 * \brief iterate over queue elements
 */
//#define queue_for(n, q) for (struct qnode *(n) = (q)->head; (n); (n) = (n)->next)

impl Queue {
    /** create a new statically allocated queue */
    pub const fn empty() -> Self {
        Self {
            count: 0,
            head: core::ptr::null_mut(),
            tail: core::ptr::null_mut(),
            flags: 0,
        }
    }

    pub fn iter<'a>(&'a self) -> QueueIterator<'a> {
        unsafe {
            QueueIterator {
                cur: &*(*self).head
            }
        }
    }
}


/*
 * \ingroup ds
 * \brief create a new dynamically allocated queue
 */
#[no_mangle]
pub unsafe extern "C" fn queue_new() -> *mut Queue {
    //struct queue *queue;

    let queue = kmalloc(core::mem::size_of::<Queue>(), &M_QUEUE, M_ZERO) as *mut Queue;

    if queue.is_null() {
        return core::ptr::null_mut();
    }

    return queue;
}

/*
 * \ingroup ds
 * \brief insert a new element in a queue
 */
#[no_mangle]
pub unsafe extern "C" fn enqueue(queue: *mut Queue, value: *mut u8) -> *mut Qnode {
    if queue.is_null() {
        return core::ptr::null_mut();
    }

    //int trace = queue->flags & QUEUE_TRACE;

    //if (trace) printk("qtrace: enqueue(%p, %p)\n", queue, value);

    //struct qnode *node;
    
    let mut node = kmalloc(core::mem::size_of::<Qnode>(), &M_QNODE, M_ZERO) as *mut Qnode;
    if node.is_null() {
        return core::ptr::null_mut();
    }

    //if (trace) printk("qtrace: allocated node %p\n", node);

    (*node).value = value;

    if (*queue).count == 0 {
        /* Queue is not initalized */
        (*queue).head = node;
        (*queue).tail = node;
    } else {
        (*node).prev = (*queue).tail;
        (*(*queue).tail).next = node;
        (*queue).tail = node;
    }

    (*queue).count += 1;

    return node;
}

/*
 * \ingroup ds
 * \brief get an element from a queue and remove it
 */
#[no_mangle]
pub unsafe extern "C" fn dequeue(queue: *mut Queue) -> *mut u8 {
    if queue.is_null() || (*queue).count == 0 {
        return core::ptr::null_mut();
    }

    (*queue).count -= 1;

    let head = (*queue).head;

    (*queue).head = (*head).next;

    if !(*queue).head.is_null() {
        (*(*queue).head).prev = core::ptr::null_mut();
    }

    if head == (*queue).tail {
        (*queue).tail = core::ptr::null_mut();
    }

    let value = (*head).value;

    kfree(head as *mut u8);

    return value;
}

/*
 * \ingroup ds
 * \brief remove an element matching a specific value from a queue
 */
#[no_mangle]
pub unsafe extern "C" fn queue_remove(queue: *mut Queue, value: *mut u8) -> () {
    if queue.is_null() || (*queue).count == 0 {
        return;
    }

    let mut qnode = (*queue).head;

    while !qnode.is_null() {
        if (*qnode).value == value {
            if (*qnode).prev.is_null() {    /* Head */
                dequeue(queue);
            } else if (*qnode).next.is_null() {   /* Tail */
                (*queue).count -= 1;
                (*queue).tail = (*(*queue).tail).prev;
                (*(*queue).tail).next = core::ptr::null_mut();
                kfree(qnode as *mut u8);
            } else {
                (*queue).count -= 1;
                (*(*qnode).prev).next = (*qnode).next;
                (*(*qnode).next).prev = (*qnode).prev;
                kfree(qnode as *mut u8);
            }

            break;
        }

        qnode = (*qnode).next;
    }
}

/*
 * \ingroup ds
 * \brief remove an element from a queue given the queue node
 */
#[no_mangle]
pub unsafe extern "C" fn queue_node_remove(queue: *mut Queue, qnode: *mut Qnode) -> () {
    if queue.is_null() || (*queue).count == 0 || qnode.is_null() {
        return;
    }

    if !(*qnode).prev.is_null() {
        (*(*qnode).prev).next = (*qnode).next;
    }

    if !(*qnode).next.is_null() {
        (*(*qnode).next).prev = (*qnode).prev;
    }

    if (*queue).head == qnode {
        (*queue).head = (*qnode).next;
    }

    if (*queue).tail == qnode {
        (*queue).tail = (*qnode).prev;
    }

    (*queue).count -= 1;

    kfree(qnode as *mut u8);

    return;
}
