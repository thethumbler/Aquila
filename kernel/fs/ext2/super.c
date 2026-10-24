/** \file
 * ext2: writing the superblock back to the disk.
 */

#include <ext2.h>

/**
 * Writes the in-memory copy of the superblock back to the disk, after a
 * change to its counts of free blocks and inodes.
 *
 * @param desc  the mounted filesystem
 * @return 0, or a negative errno if the write failed
 */
int ext2_superblock_rewrite(struct ext2 *desc)
{
    return ext2_io(desc, 1, 1024, sizeof(struct ext2_superblock), desc->superblock);
}
