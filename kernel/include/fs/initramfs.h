#ifndef _INITRAMFS_H
#define _INITRAMFS_H

#include <fs/vfs.h>

extern struct fs initramfs;

int initramfs_archiver_register(struct fs *fs);
int load_ramdisk(void);

#endif /* ! _INITRAMFS_H */
