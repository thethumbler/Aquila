#ifndef _DEVICE_H
#define _DEVICE_H

#include <core/system.h>

typedef enum
{
	CHRDEV = 1,
	BLKDEV = 2,
} dev_type;

typedef struct device dev_t;

#include <fs/vfs.h>
#include <sys/proc.h>

struct device
{
	char		*name;
	dev_type	type;
	int 		(*probe)();
	ssize_t		(*read) (struct fs_node * dev, off_t offset, size_t size, void * buf);
	ssize_t		(*write)(struct fs_node * dev, off_t offset, size_t size, void * buf);
	int			(*ioctl)(struct fs_node * dev, unsigned long request, void * argp);

	/* File Operations */
	struct file_ops f_ops;

} __packed;

void devman_init();


/* Devices */
extern dev_t i8042dev;
extern dev_t ps2kbddev;
extern dev_t condev;
extern dev_t ttydev;

#endif