/* minimal pthreads on top of the kernel thread syscalls */

#define _POSIX_THREADS
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <aquila/syscall.h>

#include "syscall.h"

#define THREAD_STACK_SIZE   8192

/* called by the kernel on the new thread's stack */
static void thread_entry(void *(*start_routine)(void *), void *arg)
{
    pthread_exit(start_routine(arg));
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
        void *(*start_routine)(void *), void *arg)
{
    char *stack = calloc(1, THREAD_STACK_SIZE);

    if (!stack)
        return ENOMEM;

    struct aquila_thread_args args = {
        .stack  = (uintptr_t) (stack + THREAD_STACK_SIZE),
        .entry  = (uintptr_t) thread_entry,
        .uentry = (uintptr_t) start_routine,
        .arg    = (uintptr_t) arg,
        .attr   = (uintptr_t) attr,
    };

    long tid = __syscall(SYS_thread_create, (long) &args, 0, 0);

    if (tid < 0) {
        free(stack);
        return -tid;
    }

    *thread = tid;
    return 0;
}

void pthread_exit(void *value_ptr)
{
    __syscall(SYS_thread_exit, (long) value_ptr, 0, 0);

    /* not reached */
    for (;;);
}

int pthread_join(pthread_t thread, void **value_ptr)
{
    long ret = __syscall(SYS_thread_join, thread, (long) value_ptr, 0);
    return ret < 0 ? -ret : 0;
}
