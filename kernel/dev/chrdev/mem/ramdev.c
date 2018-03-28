#include <core/system.h>
#include <core/string.h>

#include <dev/dev.h>
#include <dev/ramdev.h>

static ssize_t ramdev_read(struct inode *node, off_t offset, size_t _size, void *buf)
{
	/* Read ramdev private data from node */
    ramdev_private_t *p = (ramdev_private_t*) node->p;
    
    /* Maximum possible read size */
    ssize_t size = MIN(_size, node->size - offset);
    
    /* Copy `size' bytes from ramdev into buffer */
    memcpy(buf, (char *) p->addr + offset, size);

    return size;
}

static ssize_t ramdev_write(struct inode *node, off_t offset, size_t _size, void *buf)
{
	/* Read ramdev private data from inode */
    ramdev_private_t *p = (ramdev_private_t*) node->p;
    
    /* Maximum possible write size */
    ssize_t size = MIN(_size, node->size - offset);
    
    /* Copy `size' bytes from buffer to ramdev */
    memcpy((char *) p->addr + offset, buf, size);

    return size;
}

struct dev ramdev = {
	.name  = "ramdev",
	.read  = &ramdev_read,
	.write = &ramdev_write,
};
