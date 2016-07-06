#ifndef _INITRAMFS_H
#define _INITRAMFS_H

#include <core/system.h>
#include <fs/vfs.h>

extern struct fs initramfs;

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
  struct fs_node * super;
  struct fs_node * parent;
  struct fs_node * dir;
  size_t count;
  size_t data; /* offset of data in the archive */

  struct fs_node * next;  /* For directories */
} cpiofs_private_t;

#define CPIO_BIN_MAGIC  070707

void load_ramdisk();

#endif /* !_INITRAMFS_H */