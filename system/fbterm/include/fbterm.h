#ifndef _FBTERM_H
#define _FBTERM_H

#include <tinyfont.h>

struct fbterm_ctx {
    unsigned cr, cc;    /* Cursor location (row, column) */
    unsigned rows;      /* Number of rows */
    unsigned cols;      /* Number of cols */
    char *wallpaper;    /* Wallpaper buffer */
    char *textbuf;      /* Rendered text buffer */
    char *backbuf;      /* Back buffer for terminal */
    struct font *font;  /* Font used */
};

#endif
