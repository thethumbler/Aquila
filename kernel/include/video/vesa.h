#ifndef _VIDEO_VESA
#define _VIDEO_VESA

#include <video/vbe.h>

/** The boot loader's VBE framebuffer: what VBE said about the card and
 * the mode (dev/fb/vesa/). */
struct fbdev_vesa {
    struct vbe_info_block *vbe_info;
    struct mode_info_block *mode_info;
};

#endif /* ! _VIDEO_VESA */
