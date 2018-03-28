#include <stdio.h>
#define _POSIX_THREADS
#include <pthread.h>

void *thread_entry(void *arg)
{
    printf("Hello, World! from thread %d\n", *(int *) arg);
    return NULL;
}

#define NR_THREADS 5

int main()
{
    pthread_t tid[NR_THREADS];
    int args[NR_THREADS] = {1, 2, 3, 4, 5};

    for (int i = 0; i < NR_THREADS; ++i) {
        pthread_create(&tid[i], NULL, thread_entry, &args[i]);
    }

    printf("Main thread!\n");

    for (int i = 0; i < NR_THREADS; ++i) {
        pthread_join(tid[i], NULL);
    }
}
