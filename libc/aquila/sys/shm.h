#ifndef _SYS_SHM_H
#define _SYS_SHM_H

#include <sys/ipc.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * System V shared memory. The kernel has none yet: the functions fail with
 * ENOSYS, programs that check (X servers and clients, MIT-SHM) go without.
 */

typedef unsigned long shmatt_t;

struct shmid_ds {
    struct ipc_perm shm_perm;
    size_t   shm_segsz;
    pid_t    shm_lpid;
    pid_t    shm_cpid;
    shmatt_t shm_nattch;
    time_t   shm_atime;
    time_t   shm_dtime;
    time_t   shm_ctime;
};

#define SHM_RDONLY  010000
#define SHM_RND     020000

#define SHMLBA      4096

int   shmget(key_t key, size_t size, int flags);
void *shmat(int id, const void *addr, int flags);
int   shmdt(const void *addr);
int   shmctl(int id, int cmd, struct shmid_ds *buf);

#ifdef __cplusplus
}
#endif

#endif /* ! _SYS_SHM_H */
