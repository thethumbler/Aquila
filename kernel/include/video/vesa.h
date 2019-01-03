#ifndef _FBDEV_VESA
#define _FBDEV_VESA

#include <video/vbe.h>

struct fbdev_vesa {
    struct vbe_info_block *vbe_info;
    struct mode_info_block *mode_info;
};

#endif /* ! _FBDEV_VESA */
