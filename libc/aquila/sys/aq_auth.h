#ifndef _SYS_AQ_AUTH_H
#define _SYS_AQ_AUTH_H

#include <sys/types.h>

/* deprecated authentication syscall used by login */
int aquila_auth(uid_t uid, const char *pw);

#endif /* ! _SYS_AQ_AUTH_H */
