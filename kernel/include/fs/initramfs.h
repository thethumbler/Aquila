#ifndef _INITRAMFS_H
#define _INITRAMFS_H

#include <core/system.h>
#include <fs/vfs.h>

extern fs_t initramfs;

typedef struct
{
  uint16_t magic;
  uint16_t dev;
  uint16_t ino;
  uint16_t mode;
  uint16_t uid;
  uint16_t gid;
  uint16_t nlink;
  uint16_t rdev;
  uint16_t mtimes[2];
  uint16_t namesize;
  uint16_t filesize[2];
} cpio_hdr_t;

typedef struct
{
  inode_t *super;
  inode_t *parent;
  inode_t *dir;
  size_t  count;
  size_t  data; /* offset of data in the archive */

  inode_t *next;  /* For directories */
} cpiofs_private_t;

#define CPIO_BIN_MAGIC  070707

void load_ramdisk();

#endif /* !_INITRAMFS_H */