/*
 * doomgeneric on AquilaOS: the framebuffer, and the keyboard of the
 * virtual terminal
 *
 * The picture is drawn in /dev/fb0 (mapped), centered; the terminal in
 * front (/dev/tty0) is put in graphics mode (the console stops drawing)
 * and its keyboard in K_MEDIUMRAW: Linux key codes, 0x80 set on release,
 * read without waiting. Both are put back on exit, and on a signal that
 * ends it.
 */

#include "doomkeys.h"
#include "doomgeneric.h"
#include "i_system.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/fb.h>
#include <sys/kd.h>

static uint8_t *fb;
static unsigned fb_width, fb_height, fb_stride, fb_bpp;

static int tty = -1;
static int saved_kd_mode, saved_kb_mode;
static struct termios saved_tios;

static struct timespec start;

/* Linux key codes (K_MEDIUMRAW), those Doom has a use for */
#define LK_ESC          1
#define LK_1            2       /* to LK_0, 11 */
#define LK_MINUS        12
#define LK_EQUAL        13
#define LK_BACKSPACE    14
#define LK_TAB          15
#define LK_Q            16      /* the row to LK_RIGHTBRACE, 27 */
#define LK_ENTER        28
#define LK_LEFTCTRL     29
#define LK_A            30      /* the row to LK_APOSTROPHE, 40 */
#define LK_LEFTSHIFT    42
#define LK_BACKSLASH    43      /* the row to LK_SLASH, 53 */
#define LK_RIGHTSHIFT   54
#define LK_LEFTALT      56
#define LK_SPACE        57
#define LK_F1           59      /* to LK_F10, 68 */
#define LK_F11          87
#define LK_F12          88
#define LK_RIGHTCTRL    97
#define LK_RIGHTALT     100
#define LK_UP           103
#define LK_LEFT         105
#define LK_RIGHT        106
#define LK_DOWN         108

static const char row_digits[] = "1234567890";
static const char row_q[] = "qwertyuiop[]";
static const char row_a[] = "asdfghjkl;'";
static const char row_z[] = "\\zxcvbnm,./";

/* Doom's key for a key code, 0 for none */
static unsigned char doom_key(int code)
{
    switch (code) {
        case LK_ESC:        return KEY_ESCAPE;
        case LK_ENTER:      return KEY_ENTER;
        case LK_TAB:        return KEY_TAB;
        case LK_BACKSPACE:  return KEY_BACKSPACE;
        case LK_MINUS:      return KEY_MINUS;
        case LK_EQUAL:      return KEY_EQUALS;
        case LK_SPACE:      return KEY_USE;
        case LK_LEFTCTRL:
        case LK_RIGHTCTRL:  return KEY_FIRE;
        case LK_LEFTSHIFT:
        case LK_RIGHTSHIFT: return KEY_RSHIFT;
        case LK_LEFTALT:
        case LK_RIGHTALT:   return KEY_LALT;
        case LK_UP:         return KEY_UPARROW;
        case LK_DOWN:       return KEY_DOWNARROW;
        case LK_LEFT:       return KEY_LEFTARROW;
        case LK_RIGHT:      return KEY_RIGHTARROW;
        case LK_F11:        return KEY_F11;
        case LK_F12:        return KEY_F12;
    }

    if (code >= LK_F1 && code < LK_F1 + 10)
        return KEY_F1 + (code - LK_F1);
    if (code >= LK_1 && code < LK_1 + 10)
        return row_digits[code - LK_1];
    if (code >= LK_Q && code < LK_Q + 12)
        return row_q[code - LK_Q];
    if (code >= LK_A && code < LK_A + 11)
        return row_a[code - LK_A];
    if (code >= LK_BACKSLASH && code < LK_BACKSLASH + 11)
        return row_z[code - LK_BACKSLASH];

    return 0;
}

/* the terminal as it was */
static void restore_tty(void)
{
    if (tty < 0)
        return;

    ioctl(tty, KDSKBMODE, saved_kb_mode);
    ioctl(tty, KDSETMODE, saved_kd_mode);
    tcsetattr(tty, TCSAFLUSH, &saved_tios);
    close(tty);
    tty = -1;
}

static void on_signal(int sig)
{
    restore_tty();
    signal(sig, SIG_DFL);
    raise(sig);
}

void DG_Init(void)
{
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    int fd = open("/dev/fb0", O_RDWR);

    if (fd < 0)
        I_Error("doom: /dev/fb0: %s (boot with a framebuffer)", strerror(errno));

    if (ioctl(fd, FBIOGET_VSCREENINFO, &var) || ioctl(fd, FBIOGET_FSCREENINFO, &fix))
        I_Error("doom: the framebuffer's geometry: %s", strerror(errno));

    fb_width  = var.xres;
    fb_height = var.yres;
    fb_bpp    = var.bits_per_pixel / 8;
    fb_stride = fix.line_length;

    if (fb_bpp != 4)
        I_Error("doom: a framebuffer of %d bits per pixel, 32 wanted", var.bits_per_pixel);

    fb = mmap(NULL, fb_stride * fb_height, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (fb == MAP_FAILED)
        I_Error("doom: mapping /dev/fb0: %s", strerror(errno));

    close(fd);

    /* the terminal in front: graphics, its keyboard's key codes */
    if ((tty = open("/dev/tty0", O_RDWR | O_NONBLOCK)) < 0)
        I_Error("doom: /dev/tty0: %s", strerror(errno));

    if (ioctl(tty, KDGETMODE, &saved_kd_mode) || ioctl(tty, KDGKBMODE, &saved_kb_mode) ||
        tcgetattr(tty, &saved_tios))
        I_Error("doom: the terminal's modes: %s", strerror(errno));

    atexit(restore_tty);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGSEGV, on_signal);
    signal(SIGABRT, on_signal);

    struct termios raw = saved_tios;
    raw.c_iflag = 0;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(tty, TCSAFLUSH, &raw);

    if (ioctl(tty, KDSETMODE, KD_GRAPHICS) || ioctl(tty, KDSKBMODE, K_MEDIUMRAW))
        I_Error("doom: the terminal's graphics, its keyboard: %s", strerror(errno));

    memset(fb, 0, fb_stride * fb_height);
    clock_gettime(CLOCK_MONOTONIC, &start);
}

void DG_DrawFrame(void)
{
    /* centered, as much as fits */
    unsigned w = DOOMGENERIC_RESX < fb_width ? DOOMGENERIC_RESX : fb_width;
    unsigned h = DOOMGENERIC_RESY < fb_height ? DOOMGENERIC_RESY : fb_height;
    uint8_t *out = fb + (fb_height - h) / 2 * fb_stride + (fb_width - w) / 2 * fb_bpp;

    for (unsigned y = 0; y < h; ++y)
        memcpy(out + y * fb_stride, DG_ScreenBuffer + y * DOOMGENERIC_RESX, w * fb_bpp);
}

void DG_SleepMs(uint32_t ms)
{
    usleep(ms * 1000);
}

uint32_t DG_GetTicksMs(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000;
}

int DG_GetKey(int *pressed, unsigned char *key)
{
    unsigned char code;

    /* the next key Doom knows, 0 when there's none now */
    while (tty >= 0 && read(tty, &code, 1) == 1) {
        unsigned char k = doom_key(code & 0x7f);

        if (k) {
            *pressed = !(code & 0x80);
            *key = k;
            return 1;
        }
    }

    return 0;
}

void DG_SetWindowTitle(const char *title)
{
    (void) title;
}

int main(int argc, char **argv)
{
    doomgeneric_Create(argc, argv);

    for (;;)
        doomgeneric_Tick();
}
