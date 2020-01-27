use crate::ds::queue::*;
use crate::include::mm::kvmem::*;
use crate::malloc_define;

malloc_define!(M_HASHMAP, "hashmap\0", "hashmap structure\0");
malloc_define!(M_HASHMAP_NODE, "hashmap-node\0", "hashmap node structure\0");

pub type hash_t = usize;

const HASHMAP_DEFAULT: usize = 20;

/*
 * \ingroup ds
 * \brief hashmap
 */
#[repr(C)]
pub struct HashMap {
    pub count: usize,

    pub buckets_nr: usize,
    pub buckets: *mut Queue,

    pub eq: unsafe extern "C" fn(_: *mut u8, _: *mut u8) -> isize,
}

/*
 * \ingroup ds
 * \brief hashmap node
 */
#[repr(C)]
pub struct HashMapNode {
    pub hash: hash_t,
    pub entry: *mut u8,
    pub qnode: *mut Qnode,
}

/*
 * \ingroup ds
 * \brief iterate over hashmap elements
 */
//#define hashmap_for(n, h) \
//    for (size_t __i__ = 0; __i__ < (h)->buckets_nr; ++__i__) \
//        queue_for ((n), &(h)->buckets[__i__])

/*
 * \ingroup ds
 * \brief create a new statically allocated hashmap
 */
//#define HASHMAP_NEW(n) &(struct hashmap) {\
//    .count = 0, \
//    .buckets_nr = HASHMAP_DEFAULT, \
//    .buckets = &(struct queue[n]){0}, \
//}

/*
 * \ingroup ds
 * \brief create a new dynamically allocated hashmap
 */
#[no_mangle]
pub unsafe extern "C" fn hashmap_new(n: usize, eq: unsafe extern "C" fn(_: *mut u8, _: *mut u8) -> isize) -> *mut HashMap {
    let mut n = if n == 0 { HASHMAP_DEFAULT } else { n };
    //if (!n) n = HASHMAP_DEFAULT;

    //struct hashmap *hashmap;

    let hashmap = kmalloc(core::mem::size_of::<HashMap>(), &M_HASHMAP, M_ZERO) as *mut HashMap;

    if hashmap.is_null() {
        return core::ptr::null_mut();
    }

    //printk("hashmap_new(%d) -> %p\n", n, hashmap);
    //memset(hashmap, 0, sizeof(struct hashmap));

    (*hashmap).eq = eq;
    (*hashmap).buckets = kmalloc(n * core::mem::size_of::<Queue>(), &M_QUEUE, M_ZERO) as *mut Queue;

    if (*hashmap).buckets.is_null() {
        kfree(hashmap as *mut u8);
        return core::ptr::null_mut();
    }

    //memset(hashmap->buckets, 0, n * sizeof(struct queue));

    (*hashmap).buckets_nr = n;

    return hashmap;
}

/*
 * \ingroup ds
 * \brief get the digest of the hash function
 */
#[no_mangle]
pub unsafe extern "C" fn hashmap_digest(id: *const u8, size: usize) -> hash_t {
    //const char *id = (const char *) _id;

    let mut hash: hash_t = 0;

    for i in 0..size {
        hash += *id.offset(i as isize) as hash_t;
    }

    return hash;
}

/*
 * \ingroup ds
 * \brief insert a new element into a hashmap
 */
#[no_mangle]
pub unsafe extern "C" fn hashmap_insert(hashmap: *mut HashMap, hash: hash_t, entry: *mut u8) -> isize {
    if hashmap.is_null() || (*hashmap).buckets_nr == 0 || (*hashmap).buckets.is_null() {
        //return -EINVAL;
        return -1;
    }

    //struct hashmap_node *node;
    
    let node = kmalloc(core::mem::size_of::<HashMapNode>(), &M_HASHMAP_NODE, M_ZERO) as *mut HashMapNode;

    if node.is_null() {
        //return -ENOMEM;
        return -1;
    }

    (*node).hash  = hash;
    (*node).entry = entry;

    let idx = hash % (*hashmap).buckets_nr;

    (*node).qnode = enqueue((*hashmap).buckets.offset(idx as isize), node as *mut u8);

    if (*node).qnode.is_null() {
        kfree(node as *mut u8);
        //return -ENOMEM;
        return -1;
    }

    (*hashmap).count += 1;

    return 0;
}

/*
 * \ingroup ds
 * \brief lookup for an element in the hashmap using the hash and the key
 */
#[no_mangle]
pub unsafe extern "C" fn hashmap_lookup(hashmap: *mut HashMap, hash: hash_t, key: *mut u8) -> *mut HashMapNode {
    if hashmap.is_null() || (*hashmap).buckets.is_null() || (*hashmap).buckets_nr == 0 || (*hashmap).count == 0 {
        return core::ptr::null_mut();
    }

    let idx = hash % (*hashmap).buckets_nr;
    let queue = (*hashmap).buckets.offset(idx as isize);

    let mut qnode = (*queue).head;

    while !qnode.is_null() {
        let hnode = (*qnode).value as *mut HashMapNode;

        if !hnode.is_null() && (*hnode).hash == hash && ((*hashmap).eq)((*hnode).entry, key) != 0 {
            return hnode;
        }

        qnode = (*qnode).next;
    }

    return core::ptr::null_mut();
}

/*
 * \ingroup ds
 * \brief replace an element in the hashmap using the hash and the key
 */
#[no_mangle]
pub unsafe extern "C" fn hashmap_replace(hashmap: *mut HashMap, hash: hash_t, key: *mut u8, entry: *mut u8) -> isize {
    if hashmap.is_null() || (*hashmap).buckets.is_null() || (*hashmap).buckets_nr == 0 || (*hashmap).count == 0 {
        //return -EINVAL;
        return -1;
    }

    let idx = hash % (*hashmap).buckets_nr;
    let queue = (*hashmap).buckets.offset(idx as isize);

    let mut qnode = (*queue).head;

    while !qnode.is_null() {
        let hnode = (*qnode).value as *mut HashMapNode;

        if !hnode.is_null() && (*hnode).hash == hash && ((*hashmap).eq)((*hnode).entry, key) != 0 {
            (*hnode).entry = entry;
            return 0;
        }

        qnode = (*qnode).next;
    }

    return hashmap_insert(hashmap, hash, entry);
}

/*
 * \ingroup ds
 * \brief remove an element from the hashmap given the hashmap node
 */
#[no_mangle]
pub unsafe extern "C" fn hashmap_node_remove(hashmap: *mut HashMap, node: *mut HashMapNode) -> () {
    if (hashmap.is_null() || (*hashmap).buckets.is_null() ||
        (*hashmap).buckets_nr == 0 || node.is_null() || (*node).qnode.is_null()) {
        return;
    }

    let idx = (*node).hash % (*hashmap).buckets_nr;
    let queue = (*hashmap).buckets.offset(idx as isize);

    queue_node_remove(queue, (*node).qnode);
    kfree(node as *mut u8);

    (*hashmap).count -= 1;
}

/*
 * \ingroup ds
 * \brief free all resources associated with a hashmap
 */
#[no_mangle]
pub unsafe extern "C" fn hashmap_free(hashmap: *mut HashMap) -> () {
    for i in 0..(*hashmap).buckets_nr {
        let queue = (*hashmap).buckets.offset(i as isize);

        let mut node = dequeue(queue) as *mut HashMapNode;

        while !node.is_null() {
            kfree(node as *mut u8);
            node = dequeue(queue) as *mut HashMapNode;
        }
    }

    kfree((*hashmap).buckets as *mut u8);
    kfree(hashmap as *mut u8);
}
