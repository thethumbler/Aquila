#ifndef _DEVICE_H
#define _DEVICE_H

#include <core/system.h>

typedef enum
{
	DEV_CHR = 1,
	DEV_BLK = 2,
} dev_type;

typedef struct dev_struct dev_t;

#include <fs/vfs.h>

struct dev_struct
{
	char		*name;
	dev_type	type;
	int 		(*probe)(inode_t *dev);
	size_t		(*read) (inode_t *dev, size_t offset, size_t size, void *buf);
	size_t		(*write)(inode_t *dev, size_t offset, size_t size, void *buf);
	int			(*ioctl)(inode_t *dev, unsigned long request, va_list args);
} __attribute__((packed));

#endif