#define _POSIX_THREADS
#include <pthread.h>
#include <errno.h>

#include "syscalls.h"

int pthread_join(pthread_t thread, void **value_ptr)
{
    int ret;
    SYSCALL2(ret, SYS_THREAD_JOIN, thread, value_ptr);

    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return ret;
}
