#ifndef _DEVICE_H
#define _DEVICE_H

#include <core/system.h>
#include <va_list.h>

typedef enum
{
	DEV_CHR = 1,
	DEV_BLK = 2,
} dev_type;

typedef struct dev_struct dev_t;

struct dev_struct
{
	char       *name;
    dev_type   type;
    size_t     (*probe)(dev_t *dev);
    size_t     (*read) (dev_t *dev, size_t offset, size_t size, void *buf);
	size_t     (*write)(dev_t *dev, size_t offset, size_t size, void *buf);
	size_t	   (*ioctl)(dev_t *dev, size_t request, va_list args);
    void       *p;  /* Private data */
} __attribute__((packed));

#endif
