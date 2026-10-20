#ifndef _AQUILA_VT_H
#define _AQUILA_VT_H

/* virtual terminal switching, the ioctl numbers and structures of
 * Linux's <linux/vt.h> */

#define VT_OPENQRY      0x5600  /* the first free terminal (int) */

struct vt_mode {
    char  mode;     /* VT_AUTO or VT_PROCESS */
    char  waitv;    /* unused */
    short relsig;   /* signal sent to release the terminal */
    short acqsig;   /* signal sent once the terminal is acquired */
    short frsig;    /* unused */
};

#define VT_GETMODE      0x5601  /* (struct vt_mode) */
#define VT_SETMODE      0x5602  /* (struct vt_mode) */
#define VT_AUTO         0x00    /* switches without asking */
#define VT_PROCESS      0x01    /* switches once the process lets it go */
#define VT_ACKACQ       0x02    /* VT_RELDISP: acquiring acknowledged */

struct vt_stat {
    unsigned short v_active;    /* the active terminal */
    unsigned short v_signal;    /* unused */
    unsigned short v_state;     /* bit n: terminal n is open */
};

#define VT_GETSTATE     0x5603  /* (struct vt_stat) */
#define VT_RELDISP      0x5605  /* 1: release, 0: refuse, VT_ACKACQ */
#define VT_ACTIVATE     0x5606  /* switch to the terminal (its number) */
#define VT_WAITACTIVE   0x5607  /* wait until it is active */
#define VT_DISALLOCATE  0x5608  /* free a terminal (they are kept: nothing) */

#endif /* ! _AQUILA_VT_H */
