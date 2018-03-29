#include <ext2.h>

void ext2_superblock_rewrite(struct __ext2 *desc)
{
    vfs_write(desc->supernode, 1024, sizeof(struct ext2_superblock), desc->superblock);
}
