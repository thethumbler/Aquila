#include <core/panic.h>
#include <fs/mbr.h>
#include <fs/devfs.h>

/*
 * TODO: Make readmbr for any generic device not just ata
 */

#define BLOCK_SIZE 512
void readmbr(struct inode *node)
{
    mbr_t mbr;
    memset(&mbr, 0, sizeof(mbr_t));

    vfs.read(node, 0, sizeof(mbr_t), &mbr);
    int len = strlen(node->name);
    char name[len + 10];

    for (int i = 0; i < 4; ++i) {
        if (mbr.ptab[i].type == MBR_TYPE_UNUSED)
            continue;

        snprintf(name, 20, "%s%d", node->name, i+1);

        struct inode *n = NULL;

        struct uio uio = {
            .uid  = ROOT_UID,
            .gid  = DISK_GID,
            .mask = 0660,
        };

        if (vfs.create(&vdev_root, name, &uio, &n)) {
            panic("Could not create file");
        }

        n->type = FS_BLKDEV;
        n->dev = node->dev;
        n->p = node->p;
        n->offset = mbr.ptab[i].start_lba * BLOCK_SIZE;
    }
}
