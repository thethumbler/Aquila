#ifndef _FS_BCACHE_H
#define _FS_BCACHE_H

#include <fs/vfs.h>
#include <ds/hashmap.h>

/**
 * \ingroup vfs
 * \brief block cache
 */
struct bcache {
    struct hashmap *hashmap;
};

void bcache_init(struct bcache *bcache);
int bcache_insert(struct bcache *bcache, uint64_t off, void *data);
int bcache_remove(struct bcache *bcache, uint64_t off);
void *bcache_find(struct bcache *bcache, uint64_t off);

MALLOC_DECLARE(M_BCACHE);
MALLOC_DECLARE(M_CACHE_BLOCK);

#endif  /* ! _FS_BCACHE_H */

