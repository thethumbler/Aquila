#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <libnk/nuklear.h>
#include <nk/window.h>
#define RAWFB_RGBX_8888
#define NK_RAWFB_IMPLEMENTATION
#include <nk/rawfb.h>

#include <aquila/fb.h>
#include <aquila/input.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define WALLPAPER       1
#define WINDOW_WIDTH    1024
#define WINDOW_HEIGHT   768

#define UNUSED(a) (void)a
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) < (b) ? (b) : (a))
#define LEN(a) (sizeof(a)/sizeof(a)[0])

struct ctx {
    int socket;
    unsigned char tex_scratch[512 * 512];
    struct aquilafb fb;
    void *backbuf;
    struct rawfb_context *rawfb;
};

int compositor_init(struct ctx *ctx)
{
    int err = 0;

    /* Initialize Aquila framebuffer */
    err = aquilafb_init(&ctx->fb, "/dev/fb0");
    ctx->backbuf = calloc(1, ctx->fb.yres * ctx->fb.pitch);

    if (err || !ctx->backbuf) {
        return -1;
    }

    /* Initialize connection */
    if ((ctx->socket = connection_init()) < 0) {
        return -1;
    }

    /* Initialize rawfb */
    ctx->rawfb = nk_rawfb_init(ctx->backbuf, ctx->tex_scratch, ctx->fb.xres, ctx->fb.yres, ctx->fb.pitch);

    if (!ctx->rawfb) {
        return -1;
    }
}

void nkui_cmd_process(struct nk_command *cmd, struct nk_rect *clip)
{
    switch (cmd->type) {
        case NK_COMMAND_NOP: break;
        case NK_COMMAND_SCISSOR: {
            struct nk_command_scissor *s = (struct nk_command_scissor*)cmd;
            s->x += clip->x;
            s->y += clip->y;
        } break;
        case NK_COMMAND_LINE: {
            struct nk_command_line *l = (struct nk_command_line*)cmd;
            l->begin.x += clip->x;
            l->begin.y += clip->y;
            l->end.x   += clip->x;
            l->end.y   += clip->y;
        } break;
#if 0
        case NK_COMMAND_CURVE: {
            struct nk_command_curve *q = (const struct nk_command_curve*)cmd;
            nk_draw_list_stroke_curve(&ctx->draw_list, nk_vec2(q->begin.x, q->begin.y),
                nk_vec2(q->ctrl[0].x, q->ctrl[0].y), nk_vec2(q->ctrl[1].x,
                q->ctrl[1].y), nk_vec2(q->end.x, q->end.y), q->color,
                config->curve_segment_count, q->line_thickness);
        } break;
#endif
        case NK_COMMAND_RECT: {
            struct nk_command_rect *r = (struct nk_command_rect*)cmd;
            r->x += clip->x;
            r->y += clip->y;
        } break;
        case NK_COMMAND_RECT_FILLED: {
            struct nk_command_rect_filled *r = (struct nk_command_rect_filled*)cmd;
            r->x += clip->x;
            r->y += clip->y;
        } break;
        case NK_COMMAND_RECT_MULTI_COLOR: {
            struct nk_command_rect_multi_color *r = (struct nk_command_rect_multi_color*)cmd;
            r->x += clip->x;
            r->y += clip->y;
        } break;
        case NK_COMMAND_CIRCLE: {
            struct nk_command_circle *c = (struct nk_command_circle*)cmd;
            c->x += clip->x;
            c->y += clip->y;
        } break;
        case NK_COMMAND_CIRCLE_FILLED: {
            struct nk_command_circle_filled *c = (struct nk_command_circle_filled *)cmd;
            c->x += clip->x;
            c->y += clip->y;
        } break;
#if 0
        case NK_COMMAND_ARC: {
            const struct nk_command_arc *c = (const struct nk_command_arc*)cmd;
            nk_draw_list_path_line_to(&ctx->draw_list, nk_vec2(c->cx, c->cy));
            nk_draw_list_path_arc_to(&ctx->draw_list, nk_vec2(c->cx, c->cy), c->r,
                c->a[0], c->a[1], config->arc_segment_count);
            nk_draw_list_path_stroke(&ctx->draw_list, c->color, NK_STROKE_CLOSED, c->line_thickness);
        } break;
        case NK_COMMAND_ARC_FILLED: {
            const struct nk_command_arc_filled *c = (const struct nk_command_arc_filled*)cmd;
            nk_draw_list_path_line_to(&ctx->draw_list, nk_vec2(c->cx, c->cy));
            nk_draw_list_path_arc_to(&ctx->draw_list, nk_vec2(c->cx, c->cy), c->r,
                c->a[0], c->a[1], config->arc_segment_count);
            nk_draw_list_path_fill(&ctx->draw_list, c->color);
        } break;
        case NK_COMMAND_TRIANGLE: {
            const struct nk_command_triangle *t = (const struct nk_command_triangle*)cmd;
            nk_draw_list_stroke_triangle(&ctx->draw_list, nk_vec2(t->a.x, t->a.y),
                nk_vec2(t->b.x, t->b.y), nk_vec2(t->c.x, t->c.y), t->color,
                t->line_thickness);
        } break;
        case NK_COMMAND_TRIANGLE_FILLED: {
            const struct nk_command_triangle_filled *t = (const struct nk_command_triangle_filled*)cmd;
            nk_draw_list_fill_triangle(&ctx->draw_list, nk_vec2(t->a.x, t->a.y),
                nk_vec2(t->b.x, t->b.y), nk_vec2(t->c.x, t->c.y), t->color);
        } break;
        case NK_COMMAND_POLYGON: {
            int i;
            const struct nk_command_polygon*p = (const struct nk_command_polygon*)cmd;
            for (i = 0; i < p->point_count; ++i) {
                struct nk_vec2 pnt = nk_vec2((float)p->points[i].x, (float)p->points[i].y);
                nk_draw_list_path_line_to(&ctx->draw_list, pnt);
            }
            nk_draw_list_path_stroke(&ctx->draw_list, p->color, NK_STROKE_CLOSED, p->line_thickness);
        } break;
        case NK_COMMAND_POLYGON_FILLED: {
            int i;
            const struct nk_command_polygon_filled *p = (const struct nk_command_polygon_filled*)cmd;
            for (i = 0; i < p->point_count; ++i) {
                struct nk_vec2 pnt = nk_vec2((float)p->points[i].x, (float)p->points[i].y);
                nk_draw_list_path_line_to(&ctx->draw_list, pnt);
            }
            nk_draw_list_path_fill(&ctx->draw_list, p->color);
        } break;
        case NK_COMMAND_POLYLINE: {
            int i;
            const struct nk_command_polyline *p = (const struct nk_command_polyline*)cmd;
            for (i = 0; i < p->point_count; ++i) {
                struct nk_vec2 pnt = nk_vec2((float)p->points[i].x, (float)p->points[i].y);
                nk_draw_list_path_line_to(&ctx->draw_list, pnt);
            }
            nk_draw_list_path_stroke(&ctx->draw_list, p->color, NK_STROKE_OPEN, p->line_thickness);
        } break;
#endif
        case NK_COMMAND_TEXT: { /* FIXME */
            struct nk_command_text *t = (struct nk_command_text*)cmd;
            extern struct rawfb_context *__rawfb;
            struct nk_user_font *user_font = &__rawfb->atlas.fonts->handle;

            t->font = user_font;
            t->x += clip->x;
            t->y += clip->y;
        } break;
        case NK_COMMAND_IMAGE: {
            struct nk_command_image *i = (struct nk_command_image*)cmd;
            i->x += clip->x;
            i->y += clip->y;
        } break;
#if 0
        case NK_COMMAND_CUSTOM: {
            const struct nk_command_custom *c = (const struct nk_command_custom*)cmd;
            c->callback(&ctx->draw_list, c->x, c->y, c->w, c->h, c->callback_data);
        } break;
#endif
        default: break;
    }
}

#define DEFAULT_WALLPAPER   "/usr/share/wallpapers/wallpaper.jpg"

struct rawfb_context *__rawfb;
int main(int argc, char **argv)
{
    struct ctx ctx;
    compositor_init(&ctx);
    struct rawfb_context *rawfb = ctx.rawfb;
    __rawfb = rawfb;

    /* Compositor */
    int running = 1;

    /* Input */
    extern void cb_keyboard(void *, int, int);
    extern void cb_mouse(void *, int, int, int);
    struct aq_input input;
    aq_input_init(&input, cb_keyboard, cb_mouse, ctx.rawfb);


#if WALLPAPER
    struct aquilafb_image *wp_handle = wallpaper_set(DEFAULT_WALLPAPER);
#endif
    
    /* Window layout padding is client responsibility */
    ctx.rawfb->ctx.style.window.padding = nk_vec2(0, 0);

    int about_window = 0, calc_window = 0, term_window = 0, editor_window = 0;

    while (running) {
        /* Input */
        nk_input_begin(&ctx.rawfb->ctx);

        aq_input_handler(&input);

        nk_input_end(&ctx.rawfb->ctx);

        /* GUI */

        /* Bar */
        int barheight = 0.05 * WINDOW_HEIGHT;
        int rowheight = 0.75 * barheight;
        if (nk_begin(&rawfb->ctx, "bar", nk_rect(0, WINDOW_HEIGHT-barheight, WINDOW_WIDTH, barheight), NK_WINDOW_BACKGROUND)) {
            nk_layout_row_static(&rawfb->ctx, rowheight, 80, INT_MAX);

            if (nk_button_text(&rawfb->ctx, "Exit", 4))
                running = 0;

            if (nk_button_text(&rawfb->ctx, "About", 5)) {
                if (!fork()) {
                    execve("/usr/bin/nk-about", 0, 0);
                }
            }

            if (nk_button_text(&rawfb->ctx, "Calc", 4)) {
            }

            if (nk_button_text(&rawfb->ctx, "Editor", 6)) {
            }
        }
        nk_end(&rawfb->ctx);

#if 1
        extern struct nkui_window *windows;
        struct nkui_window *window = windows;

        while (window) {
            if (nk_begin_titled(&rawfb->ctx, window->name, window->title, window->bounds, window->flags)) {

                struct nk_window *nk_window = rawfb->ctx.current;
                struct nk_rect content = nk_window->layout->clip;

#if 0

                /* Input stack */
                if (rawfb->ctx.end == nk_win) {
                    if (nk_input_is_mouse_hovering_rect(&rawfb->ctx.input, content)) {
                        nk_input_begin(&win->rawfb->ctx);
                        float rel_x, rel_y;
                        rel_x = rawfb->ctx.input.mouse.pos.x - content.x;
                        rel_y = rawfb->ctx.input.mouse.pos.y - content.y;

                        nk_input_motion(&win->rawfb->ctx, rel_x, rel_y);

                        nk_input_end(&win->rawfb->ctx);
                    } else {
                        nk_input_begin(&win->rawfb->ctx);
                        nk_input_motion(&win->rawfb->ctx, -1, -1);
                        nk_input_end(&win->rawfb->ctx);
                    }
                }
                //nk_push_custom(&nk_win->buffer, content, NULL, nk_handle_ptr(&window->image));
#endif

                if (window->cmdbuf) {
                    struct nk_command *cmd = (struct nk_command *) window->cmdbuf;
                    struct nk_command *_cmd;

                    nk_size offset  = 0;
                    nk_size size    = window->cmdbuf_size;
                    nk_size cmdoff  = sizeof(struct nk_command);
                    nk_byte *buffer = window->cmdbuf;

                    while (cmd) {
                        nk_size cmd_size = cmd->next;
                        _cmd = nk_command_buffer_push(&nk_window->buffer, cmd->type, cmd_size);
                        memcpy((nk_byte *) _cmd + cmdoff, (nk_byte *) cmd + cmdoff, cmd_size - cmdoff);
                        nkui_cmd_process(_cmd, &content);

                        if (cmd->next >= size)
                            break;

                        cmd = (struct nk_command *) (buffer + cmd->next);
                    }
                }
            }

            nk_end(&rawfb->ctx);
            window = window->next;
        }
#endif

        /* Render wallpaper */
#if WALLPAPER
        aquilafb_wallpaper(&ctx.fb, ctx.backbuf, wp_handle);
#endif
        /* Draw rawfb context */
        nk_rawfb_render(ctx.rawfb, nk_rgb(0xec,0xf0,0xf1), 0);

        /* Render framebuffer */
        aquilafb_render(&ctx.fb, ctx.backbuf);
    }

    nk_rawfb_shutdown(ctx.rawfb);
    aq_input_end(&input);
    return 0;
}
