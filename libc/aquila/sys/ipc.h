#ifndef _SYS_IPC_H
#define _SYS_IPC_H

#include <sys/types.h>

/* System V IPC: the kernel has none (see <sys/shm.h>) */

struct ipc_perm {
    key_t  __key;
    uid_t  uid;     /* owner */
    gid_t  gid;
    uid_t  cuid;    /* creator */
    gid_t  cgid;
    mode_t mode;
};

#define IPC_PRIVATE ((key_t) 0)

#define IPC_CREAT   01000
#define IPC_EXCL    02000
#define IPC_NOWAIT  04000

#define IPC_RMID    0
#define IPC_SET     1
#define IPC_STAT    2

key_t ftok(const char *path, int id);

#endif /* ! _SYS_IPC_H */
