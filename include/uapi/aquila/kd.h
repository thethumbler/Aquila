#ifndef _AQUILA_KD_H
#define _AQUILA_KD_H

/* console (virtual terminal) display and keyboard modes, the ioctl
 * numbers and values of Linux's <linux/kd.h> */

#define KDGKBTYPE   0x4B33  /* keyboard type (char) */
#define KB_101      0x02

#define KDSETMODE   0x4B3A  /* text or graphics, the argument is the mode */
#define KDGETMODE   0x4B3B  /* (int) */
#define KD_TEXT     0x00
#define KD_GRAPHICS 0x01

#define KDGKBMODE   0x4B44  /* what the keyboard sends (int) */
#define KDSKBMODE   0x4B45  /* the argument is the mode */
#define K_RAW       0x00    /* scancodes */
#define K_XLATE     0x01    /* characters */
#define K_MEDIUMRAW 0x02    /* key codes */
#define K_UNICODE   0x03    /* characters (UTF-8) */
#define K_OFF       0x04    /* nothing */

#endif /* ! _AQUILA_KD_H */
