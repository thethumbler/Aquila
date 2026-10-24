#ifndef _DEV_UART_H
#define _DEV_UART_H

#include <ds/queue.h>

#include <fs/vfs.h>
#include <dev/tty.h>

/*
 * Serial ports (dev/tty/uart/uart.c): a UART is a terminal, /dev/ttyS0
 * and up. A UART driver (8250, PL011) provides the operations below, and
 * calls uart_receive_handler() and uart_transmit_handler() from its
 * interrupt handler.
 */

/** A serial port. */
struct uart {
    char *name;

    struct ringbuf *in;
    struct ringbuf *out;

    struct tty   *tty;      /* its terminal, once it has been opened */
    struct vnode *vnode;    /* the node it was first opened through (held) */

    /* the readers and writers of every node of it sleep on these, so
     * that the interrupt handler doesn't need a vnode that may be gone */
    struct waitq read_queue;
    struct waitq write_queue;

    void    (*init)    (struct uart *u);
    ssize_t (*transmit)(struct uart *u, char c);
    char    (*receive) (struct uart *u);
};

/* Registers a UART as /dev/ttyS<id> (-1: the first free one). */
int  uart_register(int id, struct uart *u);

/* Called by a driver when it has received `size` bytes, or has room to
 * send `size` more. */
void uart_receive_handler(struct uart *u, size_t size);
void uart_transmit_handler(struct uart *u, size_t size);

#endif /* ! _DEV_UART_H */
