/*
 * System V shared memory and IPC keys: the kernel has no System V IPC, the
 * calls fail (ENOSYS) and programs go without.
 */

#include <errno.h>
#include <sys/shm.h>
#include <sys/stat.h>

int shmget(key_t key, size_t size, int flags)
{
    (void) key; (void) size; (void) flags;
    errno = ENOSYS;
    return -1;
}

void *shmat(int id, const void *addr, int flags)
{
    (void) id; (void) addr; (void) flags;
    errno = ENOSYS;
    return (void *) -1;
}

int shmdt(const void *addr)
{
    (void) addr;
    errno = ENOSYS;
    return -1;
}

int shmctl(int id, int cmd, struct shmid_ds *buf)
{
    (void) id; (void) cmd; (void) buf;
    errno = ENOSYS;
    return -1;
}

/* the key of a file, as glibc makes it */
key_t ftok(const char *path, int id)
{
    struct stat st;

    if (stat(path, &st) < 0)
        return (key_t) -1;

    return (key_t) ((st.st_ino & 0xffff) | ((st.st_dev & 0xff) << 16) |
                    ((unsigned) (id & 0xff) << 24));
}
