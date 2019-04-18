#ifndef _ICACHE_H
#define _ICACHE_H

#include <fs/vfs.h>
#include <ds/queue.h>

struct icache {
    struct queue *inodes;
};

void icache_init(struct icache *icache);
int icache_insert(struct icache *icache, struct inode *inode);
int icache_remove(struct icache *icache, struct inode *inode);
struct inode *icache_find(struct icache *icache, ino_t ino);

MALLOC_DECLARE(M_ICACHE);

#endif  /* ! _ICACHE_H */
