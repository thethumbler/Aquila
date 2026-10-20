#ifndef _AQUILA_POLL_H
#define _AQUILA_POLL_H

/* poll(2) */
struct pollfd {
    int     fd;
    short   events;     /* requested */
    short   revents;    /* returned */
};

typedef unsigned int nfds_t;

#define POLLIN      0x0001  /* data can be read without blocking */
#define POLLPRI     0x0002  /* priority data, never set */
#define POLLOUT     0x0004  /* data can be written without blocking */
#define POLLERR     0x0008  /* error (returned only) */
#define POLLHUP     0x0010  /* hung up (returned only) */
#define POLLNVAL    0x0020  /* not an open descriptor (returned only) */
#define POLLRDNORM  POLLIN
#define POLLWRNORM  POLLOUT

#endif /* ! _AQUILA_POLL_H */
