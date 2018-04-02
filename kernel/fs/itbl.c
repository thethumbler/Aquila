#include <core/system.h>
#include <fs/itbl.h>

void itbl_init(struct itbl *itbl)
{
    itbl->tbl = queue_new();
}

int itbl_insert(struct itbl *itbl, struct inode *inode)
{
    if (!itbl)
        return -EINVAL;

    if (!itbl->tbl)
        itbl_init(itbl);

    enqueue(itbl->tbl, inode);

    return 0;
}

int itbl_remove(struct itbl *itbl, struct inode *inode)
{
    if (!itbl || !itbl->tbl)
        return -EINVAL;

    queue_remove(itbl->tbl, inode);

    return 0;
}

struct inode *itbl_find(struct itbl *itbl, vino_t id)
{
    if (!itbl || !itbl->tbl)
        return NULL;

    forlinked (node, itbl->tbl->head, node->next) {
        struct inode *inode = (struct inode *) node->value;
        if (inode->id == id) {
            return inode;
        }
    }

    return NULL;
}
