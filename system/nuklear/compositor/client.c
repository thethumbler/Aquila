#include <stdio.h>
#include <stdlib.h>
#include <nk/window.h>
#include <sys/socket.h>

#include <libnk/nuklear.h>
#include <libnk/libnk.h>
#include <nk/rawfb.h>

struct nkui_window *nkui_window_create(int client, char *title, struct nk_rect bounds, int flags)
{
    struct nkui_window *window = malloc(sizeof(struct nkui_window));
    memset(window, 0, sizeof(struct nkui_window));

    char *id = malloc(12);

    snprintf(id, 12, "%p", window);

    window->client = client;
    window->name   = id;
    window->title  = strdup(title);
    window->bounds = bounds;
    window->flags  = flags;

    window_lst_insert(window);

    return window;
}

struct nkui_window *nkui_request_window_create(int client, struct libnk_request *req)
{
    char buf[req->payload];

    ssize_t ret;
    if ((ret = recv(client, buf, sizeof(buf), MSG_WAITALL)) > 0) {
        struct libnk_request_window_create *window_req = 
            (struct libnk_request_window_create *) buf;

        struct nkui_window *window = 
            nkui_window_create(client, window_req->title, window_req->bounds, window_req->flags);

        struct libnk_reply reply;
        struct libnk_reply_window_create res;

        reply.type  = LIBNK_REPLY_ERROR;
        reply.error = 0;
        reply.payload = sizeof(res);

        res.id = window;

        send(client, &reply, sizeof(reply), MSG_WAITALL);
        send(client, &res, sizeof(res), MSG_WAITALL);
    }

    return NULL;
}

void nkui_request_cmd_push(int client, struct libnk_request *req)
{
    char buf[req->payload];
    ssize_t ret;
    if ((ret = recv(client, buf, sizeof(buf), MSG_WAITALL)) > 0) {
        struct libnk_request_cmd_push *cmds =
            (struct libnk_request_cmd_push *) buf;

        if (cmds->id) {
            struct nkui_window *window = (struct nkui_window *) cmds->id;

            if (window->cmdbuf)
                free(window->cmdbuf);

            window->cmdbuf_size = cmds->size;
            window->cmdbuf      = malloc(cmds->size);

            memcpy(window->cmdbuf, cmds->buf, cmds->size);
        }
    }
}

void nkui_request_font_width(int client, struct libnk_request *req)
{
    char buf[req->payload];
    ssize_t ret;
    if ((ret = recv(client, buf, sizeof(buf), MSG_WAITALL)) > 0) {
        struct libnk_request_font_width *font = 
            (struct libnk_request_font_width *) buf;

        extern struct rawfb_context *__rawfb;

        struct nk_user_font *user_font = &__rawfb->atlas.fonts->handle;
        float width = user_font->width(user_font->userdata, font->h, font->s, font->len);

        struct libnk_reply reply;
        reply.type    = LIBNK_REPLY_ERROR;
        reply.payload = 0;

        memcpy(&reply.error, &width, sizeof(float));
        send(client, &reply, sizeof(reply), MSG_WAITALL);
    }
}

void nkui_request_pixbuf_create(int client, struct libnk_request *req)
{
    char buf[req->payload];
    ssize_t ret;

    if ((ret = recv(client, buf, sizeof(buf), MSG_WAITALL)) > 0) {
        struct libnk_request_pixbuf_create *pixbuf = 
            (struct libnk_request_pixbuf_create *) buf;

        struct rawfb_image *image = malloc(sizeof(struct rawfb_image));
        size_t size = pixbuf->w * pixbuf->h * 4;
        image->pixels = malloc(size);
        image->w      = pixbuf->w;
        image->h      = pixbuf->h;
        image->pitch  = pixbuf->w * 4;
        image->format = NK_FONT_ATLAS_RGBA32;

        //fprintf(stderr, "Created pixbuf %p\n", image);

        struct libnk_reply reply;
        struct libnk_reply_pixbuf_create res;

        reply.type    = LIBNK_REPLY_ERROR;
        reply.error   = 0;
        reply.payload = sizeof(res);

        res.id = image;

        send(client, &reply, sizeof(reply), MSG_WAITALL);
        send(client, &res, sizeof(res), MSG_WAITALL);
    }
}

void nkui_request_pixbuf_push(int client, struct libnk_request *req)
{
    //char buf[req->payload];
    struct libnk_request_pixbuf_push pixbuf;
    recv(client, &pixbuf, sizeof(pixbuf), MSG_WAITALL);

    fprintf(stderr, "Got %lu bytes for pixbuf %p\n", pixbuf.size, pixbuf.id);
    struct rawfb_image *image = pixbuf.id;

    recv(client, image->pixels, pixbuf.size, MSG_WAITALL);
#if 0
    ssize_t ret;

    if ((ret = recv(client, buf, sizeof(buf), MSG_WAITALL)) > 0) {
        struct libnk_request_pixbuf_push *pixbuf = 
            (struct libnk_request_pixbuf_push *) buf;

        //fprintf(stderr, "Got %lu bytes for pixbuf %p\n", pixbuf->size, pixbuf->id);
        //struct rawfb_image *image = pixbuf->id;
        //memcpy(image->pixels, pixbuf->data, pixbuf->size);
    }
#endif
}

void *client_worker(void *arg)
{
    int client = *(int *) arg;

    struct libnk_context ctx;
    ctx.socket = client;

    struct libnk_request request;

    ssize_t ret = 0;
    while ((ret = libnk_request_recv(&ctx, &request))) {
        switch (request.opcode) {
            case LIBNK_OP_NOP:
                break;
            case LIBNK_OP_WINDOW_CREATE:
                nkui_request_window_create(client, &request);
                break;
            case LIBNK_OP_WINDOW_DELETE:
                break;
            case LIBNK_OP_WINDOW_ATTR_GET:
                break;
            case LIBNK_OP_CMD_PUSH:
                nkui_request_cmd_push(client, &request);
                break;
            case LIBNK_OP_FONT_WIDTH:
                nkui_request_font_width(client, &request);
                break;
            case LIBNK_OP_PIXBUF_CREATE:
                nkui_request_pixbuf_create(client, &request);
                break;
            case LIBNK_OP_PIXBUF_PUSH:
                nkui_request_pixbuf_push(client, &request);
                break;
        }
    }

    if (ret == 0) {
        /* Client closed connection */
    } else {
        /* Error occurred */
    }
}
