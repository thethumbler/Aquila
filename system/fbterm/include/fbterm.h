#ifndef _FBTERM_H
#define _FBTERM_H

#include <tinyfont.h>
#include <stdint.h>
#include <vterm.h>

struct fbterm_ctx {
    unsigned cr, cc;    /* Cursor location (row, column) */
    unsigned rows;      /* Number of rows */
    unsigned cols;      /* Number of cols */
    char *wallpaper;    /* Wallpaper buffer */
    char *textbuf;      /* Rendered text buffer */
    char *backbuf;      /* Back buffer for terminal */
    struct font *font;  /* Font used */
    uint32_t fg_c;      /* Current fg color */
    uint32_t bg_c;      /* Current fg color */

    VTerm *vt;
    VTermScreen *screen;
};

void fbterm_set_cursor(struct fbterm_ctx *ctx, int cc, int cr);
size_t fbterm_write(struct fbterm_ctx *ctx, const char *buf, size_t size);
size_t fbterm_clear(struct fbterm_ctx *ctx);

#endif
