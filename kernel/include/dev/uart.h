#ifndef _DEV_UART_H
#define _DEV_UART_H

#include <ds/queue.h>

#include <fs/vfs.h>
#include <dev/tty.h>

struct uart {
    char *name;

    struct ringbuf *in;
    struct ringbuf *out;

    struct tty   *tty;      /* once it's opened (set up) */
    struct vnode *vnode;    /* the node first opened, the terminal's (held) */

    /* every node of it sleeps on these (it's woken here, not through a
     * vnode that may be gone) */
    struct queue read_queue;
    struct queue write_queue;

    void    (*init)    (struct uart *u);
    ssize_t (*transmit)(struct uart *u, char c);
    char    (*receive) (struct uart *u);
};

int  uart_register(int id, struct uart *u);
void uart_receive_handler(struct uart *u, size_t size);
void uart_transmit_handler(struct uart *u, size_t size);

#endif /* ! _DEV_UART_H */
