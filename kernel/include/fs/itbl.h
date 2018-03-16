#ifndef _ITBL_H
#define _ITBL_H

#include <fs/vfs.h>
#include <ds/queue.h>

struct itbl {
    queue_t *tbl;
};

void itbl_init(struct itbl *itbl);
int itbl_insert(struct itbl *itbl, struct inode *inode);
int itbl_remove(struct itbl *itbl, struct inode *inode);
struct inode *itbl_find(struct itbl *itbl, vino_t id);

#endif
