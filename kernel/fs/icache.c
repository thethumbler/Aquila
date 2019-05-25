#include <core/system.h>
#include <fs/icache.h>

MALLOC_DEFINE(M_ICACHE, "icache", "inode cache structure");

void icache_init(struct icache *icache)
{
    icache->inodes = queue_new();
}

int icache_insert(struct icache *icache, struct inode *inode)
{
    if (!icache)
        return -EINVAL;

    if (!icache->inodes)
        icache_init(icache);

    enqueue(icache->inodes, inode);

    return 0;
}

int icache_remove(struct icache *icache, struct inode *inode)
{
    if (!icache || !icache->inodes)
        return -EINVAL;

    queue_remove(icache->inodes, inode);

    return 0;
}

struct inode *icache_find(struct icache *icache, ino_t ino)
{
    if (!icache || !icache->inodes)
        return NULL;

    for (struct qnode *node = icache->inodes->head; node; node = node->next) {
        struct inode *inode = (struct inode *) node->value;
        if (inode->ino == ino) {
            return inode;
        }
    }

    return NULL;
}
