#include <core/system.h>
#include <core/string.h>

#include <dev/dev.h>
#include <dev/ramdev.h>

static size_t ramdev_read(inode_t *inode, size_t offset, size_t _size, void *buf)
{
	/* Read ramdev private data from inode */
    ramdev_private_t *p = (ramdev_private_t*) inode->p;
    
    /* Maximum possible read size */
    size_t size = MIN(_size, inode->size - offset);
    
    /* Copy `size' bytes from ramdev into buffer */
    memcpy(buf, (char*) p->addr + offset, size);

    return size;
}

static size_t ramdev_write(inode_t *inode, size_t offset, size_t _size, void *buf)
{
	/* Read ramdev private data from inode */
    ramdev_private_t *p = (ramdev_private_t*) inode->p;
    
    /* Maximum possible write size */
    size_t size = MIN(_size, inode->size - offset);
    
    /* Copy `size' bytes from buffer to ramdev */
    memcpy((char*) p->addr + offset, buf, size);

    return size;
}

dev_t ramdev = 
{
	.name = "ramdev",
	.read = &ramdev_read,
	.write = &ramdev_write,
};