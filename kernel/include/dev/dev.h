#ifndef _DEVICE_H
#define _DEVICE_H

#include <core/system.h>

typedef enum
{
	CHRDEV = 1,
	BLKDEV = 2,
} dev_type;

typedef struct dev_struct dev_t;

#include <fs/vfs.h>

struct dev_struct
{
	char		*name;
	dev_type	type;
	int 		(*probe)();
	int			(*open)(inode_t *file, int flags);
	size_t		(*read) (inode_t *dev, size_t offset, size_t size, void *buf);
	size_t		(*write)(inode_t *dev, size_t offset, size_t size, void *buf);
	int			(*ioctl)(inode_t *dev, unsigned long request, va_list args);
} __attribute__((packed));

void devman_init();


/* Devices */
extern dev_t i8042dev;
extern dev_t ps2kbddev;
extern dev_t condev;

#endif