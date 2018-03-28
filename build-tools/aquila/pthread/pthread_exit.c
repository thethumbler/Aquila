#define _POSIX_THREADS
#include <pthread.h>

#include "syscalls.h"

void pthread_exit(void *value_ptr)
{
    int ret;
    SYSCALL1(ret, SYS_THREAD_EXIT, value_ptr);
}
