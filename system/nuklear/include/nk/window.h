#ifndef _NK_WINDOWN_H
#define _NK_WINDOWN_H

#include <sys/types.h>
#include <libnk/nuklear.h>
#include <nk/rawfb.h>

struct nkui_window {
    int client;
    //struct nk_window *nk_window;
    const char *name;
    char *title;
    struct nk_rect bounds;
    int flags;

    void *cmdbuf;
    size_t cmdbuf_size;

    struct nkui_window *next;
};

#endif
