#ifndef _POLL_H_
#define _POLL_H_

/* struct pollfd, nfds_t and the POLL* events, shared with the kernel */
#include <aquila/poll.h>

#ifdef __cplusplus
extern "C" {
#endif

int poll(struct pollfd *fds, nfds_t nfds, int timeout);

#ifdef __cplusplus
}
#endif

#endif /* !_POLL_H_ */
