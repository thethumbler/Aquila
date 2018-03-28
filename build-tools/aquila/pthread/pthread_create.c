#define _POSIX_THREADS
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "syscalls.h"

struct __uthread {
    void *stack;
    void *entry;
    void *uentry;
    void *arg;
    void *attr;
} __packed;

static int sys_thread_entry(void *(*entry)(void *), void *arg)
{
    pthread_exit(entry(arg));
}

static int sys_pthread_create(void *stack, void *entry, void *arg, const void *attr)
{
    int ret;
    struct __uthread __uthread = {
        .stack  = stack,
        .entry  = sys_thread_entry,
        .uentry = entry,
        .arg    = arg,
        .attr   = attr,
    };

    SYSCALL1(ret, SYS_THREAD_CREATE, &__uthread)

    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return ret;
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void*), void *arg)
{
    void *stack = malloc(8192);
    memset(stack, 0, 8192); /* XXX */

    *thread = sys_pthread_create(stack + 8192, start_routine, arg, attr);
    return 0;
}
