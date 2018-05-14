#ifndef _AQUILA_INPUT_H
#define _AQUILA_INPUT_H

#include <termios.h>

enum {
    AQ_KEY_NULL      = 0,   /* NULL */
    AQ_KEY_CTRL_C    = 3,   /* Ctrl-c */
    AQ_KEY_CTRL_D    = 4,   /* Ctrl-d */
    AQ_KEY_CTRL_F    = 6,   /* Ctrl-f */
    AQ_KEY_CTRL_H    = 8,   /* Ctrl-h */
    AQ_KEY_TAB       = 9,   /* Tab */
    AQ_KEY_CTRL_L    = 12,  /* Ctrl+l */
    AQ_KEY_ENTER     = 13,  /* Enter */
    AQ_KEY_CTRL_Q    = 17,  /* Ctrl-q */
    AQ_KEY_CTRL_S    = 19,  /* Ctrl-s */
    AQ_KEY_CTRL_U    = 21,  /* Ctrl-u */
    AQ_KEY_ESC       = 27,  /* Escape */
    AQ_KEY_BACKSPACE = 127, /* Backspace */
};

#define AQ_KEY_SPEC     1

#define AQ_MOUSE_LEFT   1
#define AQ_MOUSE_RIGHT  2

#define AQ_INPUT_END    1

struct aq_input {
    int stdin_fd;
    int mouse_fd;

    void (*cb_keyboard)(void *cb, int key, int flags);
    void (*cb_mouse)(void *cb, int dx, int dy, int flags);

    void *cb_data;

    int flags;

    int kbd_tid;
    int mouse_tid;

    struct termios orig;
};

int aq_input_init(struct aq_input *, void *, void *, void *);
void aq_input_handler(struct aq_input *);
void aq_input_end(struct aq_input *);

#endif
