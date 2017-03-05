#include <core/system.h>
#include <core/panic.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <ds/queue.h>

queue_t *ready_queue = NEW_QUEUE;   /* Ready processes queue */
proc_t *cur_proc = NULL;

void make_ready(proc_t *proc)
{
    enqueue(ready_queue, proc);
}

int kidle = 0;
void kernel_idle()
{
    kidle = 1;
    arch_idle();
}

void spawn_proc(proc_t *proc)   /* Starts process execution */
{
    proc->spawned = 1;
    arch_spawn_proc(proc);
}

void spawn_init(proc_t *init)
{
    init_process(init);
    init->state = RUNNABLE;
    arch_sched_init();
    cur_proc = init;
    spawn_proc(init);
}

void schedule() /* Called from arch-specific timer event handler */
{
    if (!ready_queue)    /* How did we even get here? */
        panic("Processes queue is not initialized");

    if (!kidle) make_ready(cur_proc);
    kidle = 0;

    if (!ready_queue->count) /* No ready processes, idle */
        kernel_idle();

    cur_proc = dequeue(ready_queue);

    if (cur_proc->spawned)
        arch_switch_proc(cur_proc);
    else
        spawn_proc(cur_proc);
}
