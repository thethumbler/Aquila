#include <minix.h>

void minix_superblock_rewrite(struct minix *desc)
{
    vfs_write(desc->supernode, 1024, sizeof(struct minix_superblock), desc->superblock);
}
