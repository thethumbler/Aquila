/** \file
 * minix: writing the superblock back to the disk.
 */

#include <minix.h>

/**
 * Writes the in-memory copy of the superblock back to the disk.
 *
 * @param desc  the mounted filesystem
 */
void minix_superblock_rewrite(struct minix *desc)
{
    vfs_write(desc->supernode, 1024, sizeof(struct minix_superblock), desc->superblock);
}
