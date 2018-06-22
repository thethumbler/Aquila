#include <stdarg.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#define NK_IMPLEMENTATION
#define NK_LIB extern
#include <libnk/libnk.h>

LIBNK_API int
libnk_connect(struct libnk_context *ctx, const char *path)
{
    if (!ctx)
        return EINVAL;

    ctx->socket = socket(AF_UNIX, SOCK_STREAM, 0);

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, path);

    return connect(ctx->socket, (struct sockaddr *) &addr, sizeof(addr));
}

LIBNK_API int
libnk_buf_send(struct libnk_context *ctx, size_t size, void *buf)
{
    return send(ctx->socket, buf, size, MSG_WAITALL);
}

LIBNK_API int
libnk_buf_recv(struct libnk_context *ctx, size_t size, void *buf)
{
    return recv(ctx->socket, buf, size, MSG_WAITALL);
}

LIBNK_API int
libnk_request_send(struct libnk_context *ctx, int opcode, size_t payload, void *buf)
{
    if (!ctx)
        return EINVAL;

    struct libnk_request req;
    req.opcode  = opcode;
    req.payload = payload;

    send(ctx->socket, &req, sizeof(req), MSG_WAITALL);
    send(ctx->socket, buf, payload, MSG_WAITALL);

    return 0;
}

LIBNK_API int
libnk_request_recv(struct libnk_context *ctx, struct libnk_request *request)
{
    return recv(ctx->socket, request, sizeof(struct libnk_request), MSG_WAITALL);
}

LIBNK_API int
libnk_reply_send(struct libnk_context *ctx, int type, int error, size_t payload, void *buf)
{
    struct libnk_reply reply;
    reply.type    = type;
    reply.error   = error;
    reply.payload = payload;

    send(ctx->socket, &reply, sizeof(struct libnk_reply), MSG_WAITALL);

    if (payload)
        send(ctx->socket, buf, payload, MSG_WAITALL);

    return 0;
}

LIBNK_API int
libnk_reply_recv(struct libnk_context *ctx, struct libnk_reply *reply)
{
    return recv(ctx->socket, reply, sizeof(struct libnk_reply), MSG_WAITALL);
}

LIBNK_API int
libnk_window_init(struct libnk_context *libnk_ctx, struct libnk_window *window, const char *title, struct nk_rect bounds, int flags)
{
    window->title  = title;
    window->bounds = bounds;
    window->flags  = flags;

    struct nk_buffer  cmds;
    struct nk_buffer  pool;

    libnk_font_init(libnk_ctx, &window->font);

    nk_buffer_init_default(&cmds);
    nk_buffer_init_default(&pool);
    nk_init_custom(&window->ctx, &cmds, &pool, &window->font.font);
}

LIBNK_API int
libnk_window_create(struct libnk_context *ctx, struct libnk_window *window)
{
    if (!ctx || !window)
        return EINVAL;

    /* Payload */
    size_t payload = sizeof(struct libnk_request_window_create);
    payload += strlen(window->title) + 1;

    char buf[payload];
    struct libnk_request_window_create *window_req = 
        (struct libnk_request_window_create *) buf;

    window_req->bounds = window->bounds;
    window_req->flags  = window->flags;
    strcpy(window_req->title, window->title);

    libnk_request_send(ctx, LIBNK_OP_WINDOW_CREATE, payload, buf);

    struct libnk_reply reply;
    struct libnk_reply_window_create res;

    libnk_reply_recv(ctx, &reply);

    if (reply.payload == sizeof(res))
        libnk_buf_recv(ctx, sizeof(res), &res);

    window->id = res.id;
    return 0;
}

LIBNK_API int
libnk_window_cmd_push(struct libnk_context *ctx, struct libnk_window *window, size_t size, void *buf)
{
    if (!ctx || !window)
        return EINVAL;

    struct libnk_request req;
    struct libnk_request_cmd_push cmd;

    req.opcode  = LIBNK_OP_CMD_PUSH;
    req.payload = sizeof(cmd) + size;

    cmd.id   = window->id;
    cmd.size = size;

    libnk_buf_send(ctx, sizeof(req), &req);
    libnk_buf_send(ctx, sizeof(cmd), &cmd);
    libnk_buf_send(ctx, size, buf);

    return 0;
}

LIBNK_API int
libnk_pixbuf_create(struct libnk_context *ctx, struct libnk_pixbuf *pixbuf, short w, short h)
{
    struct libnk_request req;
    struct libnk_request_pixbuf_create cmd;

    req.opcode  = LIBNK_OP_PIXBUF_CREATE;
    req.payload = sizeof(cmd);

    cmd.w = w;
    cmd.h = h;

    libnk_buf_send(ctx, sizeof(req), &req);
    libnk_buf_send(ctx, sizeof(cmd), &cmd);

    struct libnk_reply reply;
    struct libnk_reply_pixbuf_create res;

    libnk_reply_recv(ctx, &reply);

    if (reply.payload == sizeof(res))
        libnk_buf_recv(ctx, sizeof(res), &res);

    pixbuf->id = res.id;

    return 0;
}

LIBNK_API int
libnk_pixbuf_push(struct libnk_context *ctx, struct libnk_pixbuf *pixbuf, struct nk_rect bounds, void *data)
{
    struct libnk_request req;
    struct libnk_request_pixbuf_push cmd;

    size_t size = (size_t) bounds.w * bounds.h * 4;

    req.opcode  = LIBNK_OP_PIXBUF_PUSH;
    req.payload = sizeof(cmd) + size;

    cmd.id     = pixbuf->id;
    cmd.bounds = bounds;
    cmd.size   = size;

    libnk_buf_send(ctx, sizeof(req), &req);
    libnk_buf_send(ctx, sizeof(cmd), &cmd);
    libnk_buf_send(ctx, size, data);

    return 0;
}

LIBNK_API float
libnk_font_width(nk_handle handle, float h, const char *s, int len)
{
    size_t payload = sizeof(struct libnk_request_font_width) + len;
    char buf[payload];

    struct libnk_request_font_width *font = 
        (struct libnk_request_font_width *) buf;

    font->h   = h;
    font->len = len;
    memcpy(font->s, s, len);

    struct libnk_font *libnk_font = handle.ptr;
    libnk_request_send(libnk_font->ctx, LIBNK_OP_FONT_WIDTH, payload, buf);

    struct libnk_reply reply;
    libnk_reply_recv(libnk_font->ctx, &reply);

    float width = *(float *) &reply.error;

    return width;
}

LIBNK_API int
libnk_font_init(struct libnk_context *ctx, struct libnk_font *libnk_font)
{
    libnk_font->ctx = ctx;
    libnk_font->font.userdata = nk_handle_ptr(libnk_font);
    libnk_font->font.height   = 15.0f;
    libnk_font->font.width    = libnk_font_width;
    return 0;
}

LIBNK_API int
libnk_render(struct libnk_context *libnk_ctx, struct libnk_window *window)
{
    struct nk_context *ctx = &window->ctx;
    const struct nk_command *cmd = nk__begin(ctx);

    nk_size size = ctx->memory.allocated;
    nk_byte *buffer = (nk_byte *) ctx->memory.memory.ptr;

    libnk_window_cmd_push(libnk_ctx, window, size, (void *) cmd);
}

LIBNK_API struct nk_context *
libnk_begin(struct libnk_context *libnk_ctx, struct libnk_window *window)
{
    if (nk_begin(&window->ctx, window->title,
            nk_rect(0, 0, window->bounds.w, window->bounds.h), NK_WINDOW_BACKGROUND))
        return &window->ctx;

    return NULL;
}

LIBNK_API void
libnk_end(struct libnk_context *libnk_ctx, struct libnk_window *window)
{
    nk_end(&window->ctx);
}
