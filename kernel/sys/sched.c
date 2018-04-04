#include <core/system.h>
#include <core/panic.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <ds/queue.h>

queue_t *ready_queue = QUEUE_NEW();   /* Ready threads queue */
thread_t *cur_thread = NULL;

void sched_thread_ready(thread_t *thread)
{
    struct queue_node *sched_node = enqueue(ready_queue, thread);
    thread->sched_queue = ready_queue;
    thread->sched_node = sched_node;
}

int kidle = 0;
void kernel_idle()
{
    kidle = 1;
    arch_idle();
}

void sched_thread_spawn(thread_t *thread)   /* Starts thread execution */
{
    thread->spawned = 1;
    arch_thread_spawn(thread);
}

void sched_init_spawn(proc_t *init)
{
    proc_init(init);
    init->cwd = strdup("/");

    arch_sched_init();

    session_new(init);

    cur_thread = (thread_t *) init->threads.head->value;
    cur_thread->state = RUNNABLE;
    sched_thread_spawn(cur_thread);
}

void schedule() /* Called from arch-specific timer event handler */
{
    if (!ready_queue)    /* How did we even get here? */
        panic("Threads queue is not initialized");

    if (!kidle)
        sched_thread_ready(cur_thread);

    kidle = 0;

    if (!ready_queue->count) /* No ready threads, idle */
        kernel_idle();

    cur_thread = dequeue(ready_queue);
    cur_thread->sched_node = NULL;

    if (cur_thread->spawned) {
        arch_thread_switch(cur_thread);
    } else {
        sched_thread_spawn(cur_thread);
    }
}
