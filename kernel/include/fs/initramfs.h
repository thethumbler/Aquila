#ifndef _FS_INITRAMFS_H
#define _FS_INITRAMFS_H

#include <fs/vfs.h>
#include <boot/boot.h>

/* The initial RAM disk (fs/initramfs/): an archive that the boot loader
 * loads, unpacked into a tmpfs as the first root filesystem. */
extern struct fs initramfs;

/* Registers an archive format (cpio). */
int initramfs_archiver_register(struct fs *fs);

/* Unpacks the boot loader's module as the root filesystem. */
int load_ramdisk(module_t *module);

#endif /* ! _FS_INITRAMFS_H */
