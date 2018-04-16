#ifndef _INITRAMFS_H
#define _INITRAMFS_H

#include <core/system.h>
#include <fs/vfs.h>

extern struct fs initramfs;

void initramfs_archiver_register(struct fs *fs);
void load_ramdisk();

#endif /* !_INITRAMFS_H */
