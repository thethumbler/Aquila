//#include <libnk/nuklear.h>
#include <nk/window.h>

struct nkui_window *windows;

int window_lst_insert(struct nkui_window *window)
{
    if (!windows) {
        windows = window;
        windows->next = 0;
    } else {
        window->next = windows;
        windows = window;
    }
}
