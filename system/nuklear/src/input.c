#include <aquila/input.h>
#include <nk/nuklear.h>
#include <nk/rawfb.h>

void cb_keyboard(struct rawfb_context *rawfb, int key, int flags)
{
    if (flags & AQ_KEY_SPEC) {
        int nk_key = 0;
        switch (key) {
        case AQ_KEY_ENTER: nk_key = NK_KEY_ENTER; break;
        case AQ_KEY_BACKSPACE: nk_key = NK_KEY_BACKSPACE; break;
        //case AQ_KEY_ESC:
        }

        nk_input_key(&rawfb->ctx, nk_key, 1); 
        nk_input_key(&rawfb->ctx, nk_key, 0); 
    } else {
        nk_input_char(&rawfb->ctx, key);
    }
}

#define WINDOW_WIDTH    1024
#define WINDOW_HEIGHT   768

void cb_mouse(struct rawfb_context *rawfb, int dx, int dy, int flags)
{
    static int mx, my;

    mx += dx;
    my -= dy;

    if (mx < 0)
        mx = 0;

    if (my < 0)
        my = 0;

    if (mx > WINDOW_WIDTH)
        mx = WINDOW_WIDTH;

    if (my > WINDOW_HEIGHT)
        my = WINDOW_HEIGHT;

    nk_input_motion(&rawfb->ctx, mx, my);

    static int _left, _right;

    int left  = flags & AQ_MOUSE_LEFT;
    int right = flags & AQ_MOUSE_RIGHT;

    if (_left != left) {
        nk_input_button(&rawfb->ctx, NK_BUTTON_LEFT, mx, my, left);
        _left = left;
    }

    if (_right != right) {
        nk_input_button(&rawfb->ctx, NK_BUTTON_RIGHT, mx, my, right);
        _right = right;
    }
}
