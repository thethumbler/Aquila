#ifndef _FS_INITRAMFS_H
#define _FS_INITRAMFS_H

#include <fs/vfs.h>
#include <boot/boot.h>

extern struct fs initramfs;

int initramfs_archiver_register(struct fs *fs);
int load_ramdisk(module_t *module);

#endif /* ! _FS_INITRAMFS_H */
