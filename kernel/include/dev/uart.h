#ifndef _UART_H
#define _UART_H

#include <fs/vfs.h>
#include <dev/tty.h>

struct __uart {

    struct ringbuf *in;
    struct ringbuf *out;

    struct tty   *tty;
    struct inode *inode;    /* Inode associated with uart device */

    ssize_t (*transmit)(struct __uart *u, char c);
    char    (*receive) (struct __uart *u);
};

int uart_register(int id, struct __uart *u);
void uart_recieve_handler(struct __uart *u, size_t size);
void uart_transmit_handler(struct __uart *u, size_t size);

#endif /* ! _UART_H */
