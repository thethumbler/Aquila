#ifndef _IOCTL_H
#define _IOCTL_H

int ioctl(int fildes, int request, ...);

struct winsize {
	unsigned short ws_row;
	unsigned short ws_col;
	unsigned short ws_xpixel;
	unsigned short ws_ypixel;
};

#define TIOCGPTN    0x80045430 
#define TCGETS   0x4000 /* Get termios struct */
#define TCSETS   0x4001 /* Set termios struct */

#endif
