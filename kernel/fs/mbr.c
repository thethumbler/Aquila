#include <core/panic.h>
#include <fs/mbr.h>
#include <fs/devfs.h>

/*
 * TODO: Make readmbr for any generic device not just ata
 */

#if 0
#define BLOCK_SIZE 512
void readmbr(struct inode *node)
{
    static mbr_t mbr;
    memset(&mbr, 0, sizeof(mbr_t));

    vfs.read(node, 0, sizeof(mbr_t), &mbr);
    int len = strlen(node->name);
    char name[len + 10];

    for (int i = 0; i < 4; ++i) {
        if (mbr.ptab[i].type == MBR_TYPE_UNUSED)
            continue;

        snprintf(name, 20, "%s%d", node->name, i+1);

        if (vfs.create(dev_root, name)) {
            panic("Could not create file");
        } else {
            struct vfs_path path = (struct vfs_path) {
                .mountpoint = dev_root,
                .tokens = (char *[]) {name, NULL}
            };

            struct fs_node *n = vfs.traverse(&path);

            if (!n) {
                panic("File not found");
            }

            n->dev = node->dev;
            n->p = node->p;
            n->offset = mbr.ptab[i].start_lba * BLOCK_SIZE;
        }
    }
}
#endif
