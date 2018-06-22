#ifndef _LIBNK_H
#define _LIBNK_H

#include <sys/types.h>
#include <libnk/nuklear.h>

#define NKUI_DEFAULT_PATH   "/tmp/nkui"

/* Context */
struct libnk_context {
    int socket;
};

struct libnk_font {
    struct nk_user_font font;
    struct libnk_context *ctx;
};

struct libnk_pixbuf {
    void *id;
    short w, h;
};

/* Window */
struct libnk_window {
    void *id;
    const char *title;
    struct nk_rect bounds;
    int flags;
    struct nk_context ctx;
    struct libnk_font font;
};

/* Request Types */
struct libnk_request {
    int opcode;
    size_t payload;
} __packed;

struct libnk_request_window_create {
    struct nk_rect bounds;
    int flags;
    char title[];
} __packed;

struct libnk_request_cmd_push {
    void *id;
    size_t size;
    char buf[];
} __packed;

struct libnk_request_font_width {
    int id;
    float h;
    int len;
    char s[];
} __packed;

struct libnk_request_pixbuf_create {
    short w, h;
} __packed;

struct libnk_request_pixbuf_push {
    void *id;
    struct nk_rect bounds;
    size_t size;
    char data[];
} __packed;

/* Reply */
#define LIBNK_REPLY_ERROR   1
#define LIBNK_REPLY_EVENT   2

struct libnk_reply {
    int type;
    int error;
    size_t payload;
} __packed;

struct libnk_reply_window_create {
    void *id;
} __packed;

struct libnk_reply_pixbuf_create {
    void *id;
} __packed;

/* OpCodes */
#define LIBNK_OP_NOP                    0
#define LIBNK_OP_WINDOW_CREATE          1
#define LIBNK_OP_WINDOW_DELETE          2
#define LIBNK_OP_WINDOW_ATTR_GET        3
#define LIBNK_OP_WINDOW_ATTR_UPDATE     4
#define LIBNK_OP_CMD_PUSH               5
#define LIBNK_OP_FONT_WIDTH             6
#define LIBNK_OP_PIXBUF_CREATE          7
#define LIBNK_OP_PIXBUF_PUSH            8

/* API */
#define LIBNK_API   extern
LIBNK_API int libnk_connect(struct libnk_context *ctx, const char *path);
LIBNK_API int libnk_buf_send(struct libnk_context *ctx, size_t size, void *buf);
LIBNK_API int libnk_buf_recv(struct libnk_context *ctx, size_t size, void *buf);
LIBNK_API int libnk_request_send(struct libnk_context *ctx, int opcode, size_t payload, void *buf);
LIBNK_API int libnk_request_recv(struct libnk_context *ctx, struct libnk_request *request);
LIBNK_API int libnk_reply_send(struct libnk_context *ctx, int type, int error, size_t payload, void *buf);
LIBNK_API int libnk_reply_recv(struct libnk_context *ctx, struct libnk_reply *reply);

LIBNK_API int libnk_window_init(struct libnk_context *ctx, struct libnk_window *window, const char *title, struct nk_rect bounds, int flags);
LIBNK_API int libnk_window_create(struct libnk_context *ctx, struct libnk_window *window);
LIBNK_API int libnk_window_cmd_push(struct libnk_context *ctx, struct libnk_window *window, size_t size, void *buf);

LIBNK_API int libnk_pixbuf_create(struct libnk_context *ctx, struct libnk_pixbuf *pixbuf, short w, short h);
LIBNK_API int libnk_pixbuf_push(struct libnk_context *ctx, struct libnk_pixbuf *pixbuf, struct nk_rect bounds, void *data);

LIBNK_API int libnk_font_init(struct libnk_context *ctx, struct libnk_font *libnk_font);
LIBNK_API int libnk_render(struct libnk_context *libnk_ctx, struct libnk_window *window);
LIBNK_API struct nk_context *libnk_begin(struct libnk_context *libnk_ctx, struct libnk_window *window);
LIBNK_API void libnk_end(struct libnk_context *libnk_ctx, struct libnk_window *window);

#endif /* ! _LIBNK_H */
