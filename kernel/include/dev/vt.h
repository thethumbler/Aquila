#ifndef _DEV_VT_H
#define _DEV_VT_H

#include <core/system.h>
#include <dev/tty.h>
#include <ds/ringbuf.h>
#include <ds/queue.h>
#include <aquila/kd.h>
#include <aquila/vt.h>

/*
 * Virtual terminals: the screen and keyboard as terminals.
 *
 * The kernel emulates a terminal (the Linux console's escape sequences,
 * TERM=linux) on the framebuffer, or in VGA text mode without one, and
 * turns key presses into what such a terminal sends. /dev/tty1 to
 * /dev/tty6 (major 4) are separate terminals, and Alt+F1 to Alt+F6 switch
 * between them.
 */

/* How many virtual terminals there are. */
#define VT_COUNT    6

/* A character's attributes. */
#define VT_BOLD         0x01
#define VT_DIM          0x02
#define VT_UNDERLINE    0x04
#define VT_BLINK        0x08
#define VT_REVERSE      0x10
#define VT_INVISIBLE    0x20
#define VT_ITALIC       0x40
#define VT_CURSOR       0x80    /* the cursor is drawn on it */

/** A character cell. Its colors are indices into the 256-color palette. */
struct vt_cell {
    uint8_t ch;     /* code page 437 */
    uint8_t attr;
    uint8_t fg;
    uint8_t bg;
};

/** A display that virtual terminals are shown on. */
struct vt_display {
    char *name;
    int rows, cols;

    /** draws a cell */
    void (*draw)(int row, int col, struct vt_cell cell);

    /** moves a hardware cursor; NULL if the cursor is drawn (VT_CURSOR) */
    void (*cursor)(int row, int col, int visible);
};

/* The displays: the framebuffer, and VGA text mode. Their probes return
 * 0 if they can be used. */
extern struct vt_display vt_fb_display;
extern struct vt_display vt_vga_display;
int vt_fb_probe(void);
int vt_vga_probe(void);

/* Returns the RGB value of a palette color. */
uint32_t vt_palette(uint8_t color);

/* Returns a cell's VGA text mode attribute. */
uint8_t vt_vga_attr(struct vt_cell cell);

/** The cursor's state, saved by DECSC. */
struct vt_cursor {
    int row, col;
    struct vt_cell pen;
    int charset[2], gl;
    int origin, wrap;
};

/** A virtual terminal: its screen, its emulator's state, and its tty. */
struct vt {
    int id;
    int rows, cols;

    /* the main screen and the alternate one; `screen` is the one shown */
    struct vt_cell *main, *alt, *screen;

    /* the cursor, and a pending wrap (a character was written in the
     * last column) */
    int row, col, wrap_pending;

    /* the attributes and colors characters are written with */
    struct vt_cell pen;

    /* the scrolling region: its top and bottom rows, inclusive */
    int top, bottom;

    /* modes */
    int autowrap, origin, insert, newline, cursor_visible, cursor_keys,
        reverse_screen, keypad;

    /* the G0 and G1 character sets ('B' for ASCII, '0' for DEC special
     * graphics), and the one in use (SO/SI) */
    int charset[2], gl;

    /* tab stops */
    uint8_t tabs[32];

    struct vt_cursor saved, saved_alt;

    /* the escape sequence parser */
    int state;
    int params[16], nparams;
    char private, intermediate;
    uint8_t last;   /* the last character written, for REP */

    /* the UTF-8 decoder */
    uint32_t utf8;
    int utf8_left;

    /* replies to requests (DSR, DA), sent as input once a write is done */
    char reply[32];
    size_t reply_len;

    /* KD_TEXT, or KD_GRAPHICS when a program draws on the framebuffer
     * and the terminal doesn't; what the keyboard sends (K_XLATE, K_RAW,
     * ...); and the process that set them, which they are undone for
     * once it is gone */
    int kd_mode, kb_mode;
    pid_t kd_pid;

    /* VT_PROCESS: switching away waits for the process (relsig, then
     * VT_RELDISP), and the process is told when the terminal is back
     * (acqsig) */
    struct vt_mode mode;
    pid_t mode_pid;

    /* how many descriptors have it open, for VT_OPENQRY */
    int opens;

    /* the tty, and the input that readers get */
    struct tty *tty;
    struct ringbuf *in;
    struct vnode *vnode;
    struct waitq read_queue, write_queue;
};

/* term.c: the terminal emulator: sets up, resets, and writes to a
 * terminal's screen */
int  vt_term_init(struct vt *vt, int rows, int cols);
void vt_term_reset(struct vt *vt);
void vt_term_write(struct vt *vt, const char *buf, size_t size);

/* vt.c: the terminals' device, input, and switching between them */
extern struct dev vtdev;
void vt_input(const char *buf, size_t size);
void vt_switch(int id);
void vt_keypress(void);
struct vt *vt_foreground(void);

/* keyboard.c: PS/2 scancodes (set 1) from the keyboard driver, turned
 * into what the terminal sends */
void vt_scancode(int scancode);
void vt_keyboard_reset(void);
int  vt_keymap(int table, int keycode);

#endif /* ! _DEV_VT_H */
