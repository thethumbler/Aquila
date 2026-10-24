#ifndef _DEV_FB_H
#define _DEV_FB_H

/* Framebuffers (dev/fb/): /dev/fb0, with Linux's framebuffer API. */

/* Registers a framebuffer, and returns the first one. */
int fbdev_register(int, void*);
struct fbdev *fbdev_primary(void);

/* The kinds of framebuffer drivers. */
#define FBDEV_TYPE_VESA 1
extern struct fbdev fbdev_vesa;

/* Linux's framebuffer API */
#include <aquila/fb.h>

/* The drivers' side */

#include <mm/vm.h>
#include <fs/iobuf.h>

/** A framebuffer: its driver, its screen's information, its operations. */
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
