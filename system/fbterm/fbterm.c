#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include <fb.h>
#include <tinyfont.h>
#include <fbterm.h>
#include <config.h>

#include <hl_vt100.h>
#include <lw_terminal_vt100.h>

#include <aqkb.h>

/* Terminals */
struct fbterm_ctx term[10];

/* Term helpers */
static void fbterm_putc(struct fbterm_ctx *ctx, char c, uint32_t color)
{
    if (ctx->cc >= ctx->cols) {
        ctx->cr++;
        ctx->cc = 0;
    }

    struct font *font = ctx->font;

    char fbuf[font->rows * font->cols];
    font_bitmap(font, fbuf, c);

    for (int i = 0; i < font->rows; ++i) {
        int cx = ctx->cc * font->cols;
        for (int j = 0; j < font->cols; ++j) {
            char v = fbuf[i * font->cols + j];
            fb_put_pixel(ctx, cx, ctx->cr*font->rows+i, _ALPHA(color, v));
            ++cx;
        }
    }

    ctx->cc++;
}

void fbterm_set_cursor(struct fbterm_ctx *ctx, int cc, int cr)
{
    ctx->cc = cc;
    ctx->cr = cr;
    fbterm_putc(ctx, '_', 0xFFFFFFFF);
}

size_t fbterm_write(struct fbterm_ctx *ctx, void *buf, size_t size)
{
	for (size_t i = 0; i < size; ++i) {
		char c = ((char *) buf)[i];

		switch (c) {
			case '\0':	/* Null-character */
				break;
			case '\n':	/* Newline */
                ctx->cr++;
                ctx->cc = 0;
				break;
			case '\b':	/* Backspace */
                ctx->cc--;
                ctx->cc = ctx->cc < 0? 0 : ctx->cc;
                fbterm_putc(ctx, ' ', 0xFFFFFFFF);
				break;
			default:
                fbterm_putc(ctx, c, 0xEEEEEEFF);
		}
	}

	return size;
}

size_t fbterm_clear(struct fbterm_ctx *ctx)
{
    fb_clear(ctx);
}

void fbterm_redraw(struct fbterm_ctx *ctx)
{
    fb_render(ctx);
}

char *fbterm_openpty(int *fd)
{
    int pty = open("/dev/ptmx", O_RDWR);

    int pts_id;
    ioctl(pty, TIOCGPTN, &pts_id);

    static char pts_fn[] = "/dev/pts/ ";
    pts_fn[9] = '0' + pts_id;

    return pts_fn;
}

struct font *font = NULL;
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
}

void disp(struct vt100_headless *vt100)
{
    const char **lines;

    lines = vt100_headless_getlines(vt100);

    fbterm_clear(&term[0]);
    term[0].cc = term[0].cr = 0;

    for (size_t y = 0; y < vt100->term->height; ++y) {
        fbterm_write(&term[0], (void *) lines[y], vt100->term->width);
        fbterm_write(&term[0], (void *) "\n", 1);
    }

    fbterm_set_cursor(&term[0], vt100->term->x, vt100->term->y);
    fbterm_redraw(&term[0]);
}

int fbterm_main()
{
    struct vt100_headless *vt100_headless;
    vt100_headless = new_vt100_headless();
    vt100_headless->changed = disp;
    vt100_headless->term = lw_terminal_vt100_init(vt100_headless, term[0].cols, term[0].rows, lw_terminal_parser_default_unimplemented);
    vt100_headless->term->ctx = &term[0];

    extern void master_write(void *user_data, void *buffer, size_t len);
    vt100_headless->term->master_write = master_write;
    vt100_headless->term->modes |= MASK_LNM;
    vt100_headless_main_loop(vt100_headless);
    return 0;
}

int main(int argc, char **argv)
{
    int pty, shell_pid;
    char *pts_fn = fbterm_openpty(&pty);

    if (fork()) {
        if (fb_init("/dev/fb0")) {
            fprintf(stderr, "Error opening framebuffer");
        }

        if (fbterm_init(&term[0]) < 0) {
            fprintf(stderr, "Error initalizing fbterm\n");
        }

        fbterm_main();
    } else {
        if (shell_pid = fork()) {
            /* Keyboard handler */
            int kbd_fd = open(KBD_PATH, O_RDONLY);
            aqkb_loop(kbd_fd, pty);
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
    }
    return 0;
}
