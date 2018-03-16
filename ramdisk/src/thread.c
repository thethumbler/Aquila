#include <stdio.h>
#include <errno.h>

#define SYS_THREAD_CREATE 29
#define SYS_THREAD_EXIT   30
#define SYS_THREAD_JOIN   31

#define SYSCALL2(ret, v, arg1, arg2) \
	asm volatile("int $0x80;":"=a"(ret):"a"(v), "b"(arg1), "c"(arg2));
#define SYSCALL1(ret, v, arg1) \
	asm volatile("int $0x80;":"=a"(ret):"a"(v), "b"(arg1));
#define SYSCALL0(ret, v) \
	asm volatile("int $0x80;":"=a"(ret):"a"(v));

struct __uthread {
    void *stack;
    void *entry;
    void *arg;
    int attr;
}__attribute__((packed));

int sys_thread_create(void *stack, void *entry, void *arg, int attr)
{
    int ret;
    struct __uthread __uthread = {
        stack = stack,
        entry = entry,
        arg   = arg,
        attr  = attr
    };

    SYSCALL1(ret, SYS_THREAD_CREATE, &__uthread);

    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return ret;
}

__attribute__((noreturn)) int thread_exit(void *value_ptr)
{
    int ret;
    SYSCALL1(ret, SYS_THREAD_EXIT, value_ptr);

    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return ret;
}

int thread_join(int tid, void **value_ptr)
{
    int ret;
    SYSCALL2(ret, SYS_THREAD_JOIN, tid, value_ptr);

    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return ret;
}

int thread_create(void *(*entry)(void *), void *arg, int attr)
{
    void *stack = malloc(8192);
    memset(stack, 0, 8192);
    return sys_thread_create(stack + 8192, entry, arg, attr);
}

void *thread_entry(void *arg)
{
    printf("Hello, World! from thread %d\n", *(int *) arg);
    fflush(stdout);
    thread_exit(NULL);
}

#define NR_THREADS 5

int main()
{
    int tid[NR_THREADS];
    int args[NR_THREADS] = {1, 2, 3, 4, 5};

    for (int i = 0; i < NR_THREADS; ++i) {
        tid[i] = thread_create(thread_entry, &args[i], NULL);
    }

    printf("Main thread!\n");
    fflush(stdout);

    //for (int i = 0; i < NR_THREADS; ++i) {
    //    thread_join(tid[i], NULL);
    //}

    thread_join(3, NULL);
}
