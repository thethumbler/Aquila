#ifndef _DEV_FB_H
#define _DEV_FB_H

int fbdev_register(int, void*);
struct fbdev *fbdev_primary(void);

#define FBDEV_TYPE_VESA 1
extern struct fbdev fbdev_vesa;

/* Linux framebuffer API */
#include <aquila/fb.h>

/* Aquila driver API */

#include <mm/vm.h>
#include <fs/iobuf.h>

struct fbdev {
    int type;
    void *data;
    struct fb_fix_screeninfo *fix_screeninfo;
    struct fb_var_screeninfo *var_screeninfo;

    int     (*probe)(int id, struct fbdev *fb);
    ssize_t (*read) (struct fbdev *fb, off_t offset, struct iobuf *io);
    ssize_t (*write)(struct fbdev *fb, off_t offset, struct iobuf *io);
    int     (*ioctl)(struct fbdev *fb, int request, void *argp);
    int     (*map)  (struct fbdev *fb, struct vm_space *vm_space, struct vm_entry *vm_entry);
};

#endif /* ! _DEV_FB_H */
