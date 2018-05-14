#include <aquila/input.h>
#include <aquila/ringbuf.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#define _POSIX_PTHREAD
#include <pthread.h>

/* XXX: Currently newlibc doesn't provide these functions */
int tcgetattr(int fd, struct termios *tios)
{
    return ioctl(fd, TCGETS, tios);
}

int tcsetattr(int fd, int req, struct termios *tios)
{
    return ioctl(fd, TCSETS, tios);
}

/* Keyboard input */
static int aq_kbd_key(struct aq_input *input, char c)
{
    static char seq[4];
    static int seq_idx = 0;
    
    if (seq_idx == 0)
        memset(seq, 0, sizeof(seq));

    seq[seq_idx++] = c;

    if (seq_idx == 4)
        seq_idx = 0;

    switch(seq[0]) {
    case AQ_KEY_ENTER:
        input->cb_keyboard(input->cb_data, AQ_KEY_ENTER, AQ_KEY_SPEC);
        //nk_input_key(&rawfb->ctx, NK_KEY_ENTER, 1);
        //nk_input_key(&rawfb->ctx, NK_KEY_ENTER, 0);
        goto reset;
    case '\b':
        input->cb_keyboard(input->cb_data, AQ_KEY_BACKSPACE, AQ_KEY_SPEC);
        //nk_input_key(&rawfb->ctx, NK_KEY_BACKSPACE, 1);
        //nk_input_key(&rawfb->ctx, NK_KEY_BACKSPACE, 0);
        goto reset;
    case AQ_KEY_ESC:    /* escape sequence */
        /* If this is just an ESC, we'll timeout here. */
        //if (read(fd,seq,1) == 0) return ESC;
        //if (read(fd,seq+1,1) == 0) return ESC;

        /* ESC [ sequences. */
        if (seq[1] == '[') {
            if (seq[2] >= '0' && seq[2] <= '9') {
                /* Extended escape, read additional byte. */
                //if (read(fd,seq+2,1) == 0) return ESC;
                if (seq[3] == '~') {
                    switch(seq[2]) {
                    case '3':
                        //input->cb_keyboard(input->cb_data, AQ_KEY_DEL, AQ_KEY_SPEC);
                        //nk_input_key(&rawfb->ctx, NK_KEY_DEL, 1); 
                        //nk_input_key(&rawfb->ctx, NK_KEY_DEL, 0); 
                        goto reset;
                    case '5':
                        //input->cb_keyboard(AQ_KEY_PAGE_UP);
                        //nk_input_key(&rawfb->ctx, NK_KEY_SCROLL_UP, 1); 
                        //nk_input_key(&rawfb->ctx, NK_KEY_SCROLL_UP, 0); 
                        goto reset;
                    case '6':
                        //nk_input_key(&rawfb->ctx, NK_KEY_SCROLL_DOWN, 1); 
                        //nk_input_key(&rawfb->ctx, NK_KEY_SCROLL_DOWN, 0); 
                        goto reset;
                    }
                }
            } else {
                /*
                switch(seq[2]) {
                case 'A':
                    nk_input_key(&rawfb->ctx, NK_KEY_UP, 1); 
                    nk_input_key(&rawfb->ctx, NK_KEY_UP, 0); 
                    goto reset;
                case 'B':
                    nk_input_key(&rawfb->ctx, NK_KEY_DOWN, 1); 
                    nk_input_key(&rawfb->ctx, NK_KEY_DOWN, 0); 
                    goto reset;
                case 'C':
                    nk_input_key(&rawfb->ctx, NK_KEY_RIGHT, 1); 
                    nk_input_key(&rawfb->ctx, NK_KEY_RIGHT, 0); 
                    goto reset;
                case 'D':
                    nk_input_key(&rawfb->ctx, NK_KEY_LEFT, 1); 
                    nk_input_key(&rawfb->ctx, NK_KEY_LEFT, 0); 
                    goto reset;
                case 'H':
                    nk_input_key(&rawfb->ctx, NK_KEY_TEXT_LINE_START, 1); 
                    nk_input_key(&rawfb->ctx, NK_KEY_TEXT_LINE_START, 0); 
                    goto reset;
                case 'F':
                    nk_input_key(&rawfb->ctx, NK_KEY_TEXT_LINE_END, 1); 
                    nk_input_key(&rawfb->ctx, NK_KEY_TEXT_LINE_END, 0); 
                    goto reset;
                }
                */
            }
        }

        /* ESC O sequences. */
        /*
        else if (seq[0] == 'O') {
            switch(seq[1]) {
            case 'H': return HOME_KEY;
            case 'F': return END_KEY;
            }
        }
        */
        break;
    default:
        //nk_input_char(&rawfb->ctx, c);
        input->cb_keyboard(input->cb_data, c, 0);
        goto reset;
    }

    return 0;

reset:
    seq_idx = 0;
    return 0;
}

static struct ringbuf *kb_buf = RINGBUF_NEW(512);
static void *aq_kbd_thread(void *arg)
{
    char k = 'a';

    for (;;) {
        if (read(0, &k, 1)) {
            ringbuf_write(kb_buf, 1, &k);
        }
    }

    return NULL;
}

static void aq_kbd_input(struct aq_input *input)
{
    while (ringbuf_available(kb_buf)) {
        char k;
        ringbuf_read(kb_buf, 1, &k);
        aq_kbd_key(input, k);
    }
}

/* Mouse input */
struct mouse_packet {
    uint8_t left    : 1;
    uint8_t right   : 1;
    uint8_t mid     : 1;
    uint8_t _1      : 1;
    uint8_t x_sign  : 1;
    uint8_t y_sign  : 1;
    uint8_t x_over  : 1;
    uint8_t y_over  : 1;
} __packed;

/*
int mouse_fd = -1;
int mx = 0, my = 0;
*/

static struct ringbuf *mouse_buf = RINGBUF_NEW(512);
static void *aq_mouse_thread(void *arg)
{
    struct aq_input *input = arg;
    uint8_t packet[3];

    for (;;) {
        if (read(input->mouse_fd, packet, 3) == 3) {
            ringbuf_write(mouse_buf, 3, packet);
        }
    }

    return NULL;
}

void aq_mouse_input(struct aq_input *input)
{
    uint8_t packet[3];
    struct mouse_packet *data;
    while (ringbuf_available(mouse_buf) >= 3) {
        ringbuf_read(mouse_buf, 3, packet);
        data = (struct mouse_packet *) &packet[0];
        int x = packet[1], y = packet[2];
        int dx = ((data->x_sign ? -256 : 0) + x) / 1;
        int dy = ((data->y_sign ? -256 : 0) + y) / 1;
        //static int _left = 0, _right = 0; /* FIXME */

        int flags = 0;
        flags |= data->left? AQ_MOUSE_LEFT : 0;
        flags |= data->right? AQ_MOUSE_RIGHT : 0;

        input->cb_mouse(input->cb_data, dx, dy, flags);

        /*
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

        static int _left = 0, _right = 0;
        if (_left != data->left) {
            nk_input_button(&rawfb->ctx, NK_BUTTON_LEFT, mx, my, data->left);
            _left = data->left;
        }

        if (_right != data->right) {
            nk_input_button(&rawfb->ctx, NK_BUTTON_RIGHT, mx, my, data->right);
            _right = data->right;
        }
        */
    }
}


int aq_input_init(struct aq_input *input, void *cb_keyboard, void *cb_mouse, void *cb)
{
    int err = 0;    /* TODO */
    pthread_t kbd_tid = 0;
    pthread_t mouse_tid = 0;

    if (!input)
        return -EINVAL;

    /* Store user callback data */
    input->cb_data = cb;
    input->cb_keyboard = cb_keyboard;
    input->cb_mouse = cb_mouse;

    /* Store original termio structure */
    tcgetattr(0, &input->orig);

    /* Enable raw mode */
    struct termios new = input->orig;
    new.c_lflag &= ~(ECHO | ECHOE | ICANON);
    tcsetattr(0, TCSAFLUSH, &new);

    /* Keyboard thread */
    pthread_create(&kbd_tid, NULL, aq_kbd_thread, input);
    input->kbd_tid = kbd_tid;

    /* Mouse thread */
    input->mouse_fd = open("/dev/mouse", O_RDONLY);
    if (input->mouse_fd < 0)
        return errno;

    pthread_create(&mouse_tid, NULL, aq_mouse_thread, input);
    input->mouse_tid = mouse_tid;

    return 0;
}

void aq_input_handler(struct aq_input *input)
{
    aq_mouse_input(input);
    aq_kbd_input(input);
}

void aq_input_end(struct aq_input *input)
{
    /* Restore original termio structure */
    tcsetattr(0, TCSAFLUSH, &input->orig);
}
