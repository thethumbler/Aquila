#include <ext2.h>

int ext2_superblock_rewrite(struct ext2 *desc)
{
    return ext2_io(desc, 1, 1024, sizeof(struct ext2_superblock), desc->superblock);
}
