#ifndef _FS_VCACHE_H
#define _FS_VCACHE_H

#include <fs/vfs.h>
#include <ds/hashmap.h>

/**
 * \ingroup vfs
 * \brief vnode cache
 */
struct vcache {
    struct hashmap *hashmap;
};

void vcache_init(struct vcache *vcache);
int vcache_insert(struct vcache *vcache, struct vnode *vnode);
int vcache_remove(struct vcache *vcache, struct vnode *vnode);
struct vnode *vcache_find(struct vcache *vcache, ino_t ino);

MALLOC_DECLARE(M_VCACHE);

#endif  /* ! _FS_VCACHE_H */
