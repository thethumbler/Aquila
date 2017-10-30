#include <fs/mbr.h>
#include <fs/devfs.h>

/*
 * TODO: Make readmbr for any generic device not just ata
 */

#define BLOCK_SIZE 512
void readmbr(struct fs_node *node)
{
    static mbr_t mbr;
    memset(&mbr, 0, sizeof(mbr_t));

    node->fs->read(node, 0, sizeof(mbr_t), &mbr);
    int len = strlen(node->name);
    char name[len + 10];

    for (int i = 0; i < 4; ++i) {
        if (mbr.ptab[i].type == MBR_TYPE_UNUSED)
            continue;

        snprintf(name, 20, "%s%d", node->name, i+1);
        printk("/dev/%s [%x][%x]\n", name, mbr.ptab[i].status, mbr.ptab[i].type);
        printk("%x => %x\n", mbr.ptab[i].start_lba, mbr.ptab[i].start_lba + mbr.ptab[i].sectors_count - 1);

        struct fs_node *n = vfs.create(dev_root, name);
        n->dev = node->dev;
        n->p = node->p;
        n->offset = mbr.ptab[i].start_lba * BLOCK_SIZE;
    }
}

