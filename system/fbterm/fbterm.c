#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>

#define _POSIX_PTHREAD
#include <pthread.h>

#include <fb.h>
#include <tinyfont.h>
#include <fbterm.h>
#include <config.h>

#include <vterm.h>

#include <aqkb.h>

/* Terminals */
struct fbterm_ctx term[10];

/* Term helpers */
static void fbterm_putc(struct fbterm_ctx *ctx, int row, int col, char c, uint32_t color)
{
    struct font *font = ctx->font;

    char fbuf[font->rows * font->cols];
    font_bitmap(font, fbuf, c);

    for (int i = 0; i < font->rows; ++i) {
        int cx = col * font->cols;
        for (int j = 0; j < font->cols; ++j) {
            char v = fbuf[i * font->cols + j];
            fb_put_pixel(ctx, cx, row*font->rows+i, _ALPHA(color, v));
            ++cx;
        }
    }
}

void fbterm_set_cursor(struct fbterm_ctx *ctx, int row, int col)
{
    fbterm_putc(ctx, row, col, '_', -1);
}

size_t fbterm_clear(struct fbterm_ctx *ctx)
{
    fb_clear(ctx);
}

void fbterm_rect_clear(struct fbterm_ctx *ctx, int r0, int r1, int c0, int c1)
{
    size_t x0 = c0 * ctx->font->cols;
    size_t x1 = c1 * ctx->font->cols;
    size_t y0 = r0 * ctx->font->rows;
    size_t y1 = r1 * ctx->font->rows;

    fb_rect_clear(ctx, x0, x1, y0, y1);
}

void fbterm_redraw(struct fbterm_ctx *ctx)
{
    fb_render(ctx);
}

char *fbterm_openpty(int *fd)
{
    int pty = open("/dev/ptmx", O_RDWR);
    *fd = pty;

    int pts_id;
    ioctl(pty, TIOCGPTN, &pts_id);

    static char pts_fn[] = "/dev/pts/ ";
    pts_fn[9] = '0' + pts_id;

    return pts_fn;
}

int damage(VTermRect rect, void *user)
{
    struct fbterm_ctx *ctx = (struct fbterm_ctx *) user;
    fbterm_rect_clear(ctx, rect.start_row, rect.end_row, rect.start_col, rect.end_col);

    VTermScreenCell cell;
    VTermPos pos;

    int row, col;
    for (row = rect.start_row; row < rect.end_row; row++) {
        for (col = rect.start_col; col < rect.end_col; col++) {
            pos.col = col;
            pos.row = row;
            vterm_screen_get_cell(ctx->screen, pos, &cell);
            fbterm_putc(ctx, row, col, cell.chars[0], _RGB(cell.fg.red, cell.fg.green, cell.fg.blue));
        }
    }

    return 1;
}

int moverect(VTermRect dest, VTermRect src, void *user)
{

}

int movecursor(VTermPos pos, VTermPos oldpos, int visible, void *user)
{
    struct fbterm_ctx *ctx = (struct fbterm_ctx *) user;

    /* Remove cursor and redraw cell */
    fbterm_rect_clear(ctx, oldpos.row, oldpos.row+1, oldpos.col, oldpos.col+1);
    VTermScreenCell cell;
    vterm_screen_get_cell(ctx->screen, oldpos, &cell);
    fbterm_putc(ctx, oldpos.row, oldpos.col, cell.chars[0], _RGB(cell.fg.red, cell.fg.green, cell.fg.blue));

    /* Set new cursor if visible */
    //if (visible)
        fbterm_set_cursor(ctx, pos.row, pos.col);
}

int settermprop(VTermProp prop, VTermValue *val, void *user)
{

}

int bell(void *user)
{

}

int resize(int rows, int cols, void *user)
{

}

int sb_pushline(int cols, const VTermScreenCell *cells, void *user)
{

}

int sb_popline(int cols, VTermScreenCell *cells, void *user)
{

}

struct font *font = NULL;
VTermScreenCallbacks screen_cbs = {
    .damage      = damage,
    .moverect    = moverect,
    .movecursor  = movecursor,
    .settermprop = settermprop,
    .bell        = bell,
    .resize      = resize,
    .sb_pushline = sb_pushline,
    .sb_popline  = sb_popline,
};

int fbterm_init(struct fbterm_ctx *ctx)
{
    if (!font) {
        font = font_open(DEFAULT_FONT);

        if (!font)
            return -1;
    }

    memset(ctx, 0, sizeof(struct fbterm_ctx));
    ctx->font = font;
    fb_term_init(ctx);
    fb_cook_wallpaper(ctx, DEFAULT_WALLPAPER);

    VTerm *vt = vterm_new(ctx->rows, ctx->cols);
    ctx->vt = vt;

    VTermScreen *sc = vterm_obtain_screen(vt);
    vterm_screen_set_callbacks(sc, &screen_cbs, ctx);
    vterm_screen_reset(sc, 0);
    ctx->screen = sc;

    return 0;
}

int kbd_fd = -1;
int pty    = -1;

int fbterm_main()
{
    struct fbterm_ctx *active = &term[0]; 

    fbterm_redraw(active);
    size_t len;
    char buf[1024];

    while ((len = read(pty, buf, sizeof(buf))) > 0) {
        /* Read input */
        vterm_input_write(active->vt, buf, len);
        fbterm_redraw(active);

        /* Write output */
        while (vterm_output_get_buffer_current(active->vt) > 0) {
            size_t s = vterm_output_read(active->vt, buf, 1024);
            write(pty, buf, s);
        }
    }

    return 0;
}

int main(int argc, char **argv)
{
    int shell_pid;
    char *pts_fn = fbterm_openpty(&pty);

    if (shell_pid = fork()) {
        if (fb_init("/dev/fb0")) {
            fprintf(stderr, "Error opening framebuffer");
        }

        if (fbterm_init(&term[0]) < 0) {
            fprintf(stderr, "Error initalizing fbterm\n");
        }

        kbd_fd = open(KBD_PATH, O_RDONLY);

        pthread_t aqkb;
        pthread_create(&aqkb, NULL, aqkb_thread, NULL);

        fbterm_main();
    } else {
        close(pty);
        int stdin_fd  = open(pts_fn, O_RDONLY);
        int stdout_fd = open(pts_fn, O_WRONLY);
        int stderr_fd = open(pts_fn, O_WRONLY);

        char *argp[] = {DEFAULT_SHELL, "sh", NULL};
        char *envp[] = {"PWD=/", "TERM=VT100", NULL};
        execve(DEFAULT_SHELL, argp, envp);
        for (;;);
    }

    return 0;
}
