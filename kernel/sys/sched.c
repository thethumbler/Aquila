#include <core/system.h>
#include <core/panic.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/signal.h>
#include <ds/queue.h>
#include <dev/random.h>
#include <sys/callout.h>
#include <fs/vfs.h>

#include <aquila/stat.h>

struct queue *ready_queue = QUEUE_NEW();   /* Ready threads queue */

/* the processors: cpus[0] the one that booted */
struct cpu cpus[CPUS_MAX];
int cpus_count;

/* threads waiting with a deadline (thread->sleep_until), on any queue */
static struct queue *timed = QUEUE_NEW();

/* nanosleep() sleeps here, only the timer wakes it */
static struct queue *naps = QUEUE_NEW();

/* processes with a real time timer armed (proc->alarm_at) */
static struct queue *alarms = QUEUE_NEW();

/*
 * called on every timer tick, `period' ns after the last one, which
 * interrupted user mode if `user' is set
 */
void sched_timer_tick(uint32_t period, int user)
{
    uint64_t now = arch_rtime_ns();

    /* when an interrupt comes, in cycles, isn't predictable */
    random_add((uint32_t) arch_cycles());

    callout_run(now);

    /* charge the tick to the running process (system calls run with
     * interrupts disabled, so kernel time is mostly missed) */
    if (!curcpu()->idle && curthread) {
        if (user)
            curproc->utime += period;
        else
            curproc->stime += period;
    }

    struct qnode *node = timed->head;

    while (node) {
        struct qnode *next = node->next;
        struct thread *thread = node->value;

        if (now >= thread->sleep_until) {
            sched_timer_cancel(thread);
            thread_wakeup(thread);
        }

        node = next;
    }

    for (node = alarms->head; node; ) {
        struct qnode *next = node->next;
        struct proc *proc = node->value;

        if (now >= proc->alarm_at) {
            if (proc->alarm_interval) {
                /* the next expiry, not every one that was missed */
                proc->alarm_at += proc->alarm_interval;
                if (proc->alarm_at <= now)
                    proc->alarm_at = now + proc->alarm_interval;
            } else {
                queue_node_remove(alarms, proc->alarm_node);
                proc->alarm_node = NULL;
            }

            signal_proc_send(proc, SIGALRM);
        }

        node = next;
    }
}

/* what is left of the real time timer of `proc', 0 if it isn't armed */
void sched_alarm_get(struct proc *proc, uint64_t *value, uint64_t *interval)
{
    uint64_t now = arch_rtime_ns();

    *value = !proc->alarm_node ? 0 : proc->alarm_at > now ? proc->alarm_at - now : 1;
    *interval = proc->alarm_node ? proc->alarm_interval : 0;
}

/*
 * The real time timer of `proc' (setitimer, alarm): SIGALRM in `value' ns,
 * then every `interval' ns (0: once), value 0 disarms it. What was left of
 * the previous one is returned in old_value and old_interval if set.
 */
void sched_alarm(struct proc *proc, uint64_t value, uint64_t interval,
                 uint64_t *old_value, uint64_t *old_interval)
{
    uint64_t now = arch_rtime_ns(), v, i;

    sched_alarm_get(proc, &v, &i);

    if (old_value)
        *old_value = v;

    if (old_interval)
        *old_interval = i;

    if (proc->alarm_node) {
        queue_node_remove(alarms, proc->alarm_node);
        proc->alarm_node = NULL;
    }

    if (!value)
        return;

    proc->alarm_at = now + value;
    proc->alarm_interval = interval;
    proc->alarm_node = enqueue(alarms, proc);
}

/* the thread no longer waits with a deadline (woken, or killed) */
void sched_timer_cancel(struct thread *thread)
{
    if (thread->timer_node) {
        queue_node_remove(timed, thread->timer_node);
        thread->timer_node = NULL;
    }
}

/*
 * Sleep on `queue' until woken up, or until arch_rtime_ns() reaches
 * `until' (0: no limit). Returns 1 if the time is up, -ERESTARTSYS if a
 * signal ended the sleep (thread_queue_sleep), 0 if woken up.
 */
int sched_wait(struct queue *queue, uint64_t until)
{
    if (until) {
        if (arch_rtime_ns() >= until)
            return 1;

        curthread->sleep_until = until;
        curthread->timer_node  = enqueue(timed, curthread);
    }

    int err = thread_queue_sleep(queue);
    sched_timer_cancel(curthread);

    if (err)
        return err;

    return until && arch_rtime_ns() >= until;
}

/*
 * Sleep until arch_rtime_ns() reaches `until': 0, or -ERESTARTSYS if a
 * signal ended the sleep before
 */
int sched_sleep(uint64_t until)
{
    int r;

    while (!(r = sched_wait(naps, until)))
        ;

    return r < 0 ? r : 0;
}

void sched_thread_ready(struct thread *thread)
{
    struct qnode *sched_node = enqueue(ready_queue, thread);
    thread->sched_queue = ready_queue;
    thread->sched_node  = sched_node;
}

/* an urgent thread was woken: switch to it when the interrupt returns to
 * user mode (an idle CPU switches anyway) */
int sched_urgent = 0;

/* a sleeping thread woken up: ready, ahead of the others if urgent */
void sched_thread_wake(struct thread *thread)
{
    if (!thread->urgent) {
        sched_thread_ready(thread);
        return;
    }

    thread->sched_queue = ready_queue;
    thread->sched_node  = enqueue_head(ready_queue, thread);
    sched_urgent = 1;
}

/* the current thread is done running (it sleeps, or exited): another
 * ready one runs now, or the CPU idles until an interrupt */
void kernel_idle(void)
{
    curcpu()->idle = 1;

    if (ready_queue->count)
        schedule();

    arch_idle();
}

/* an interrupt woke a thread up while the CPU was idle: it runs now rather
 * than at the next timer tick (a disk request would wait for one each) */
void sched_idle_wakeup(void)
{
    if (curcpu()->idle && ready_queue->count)
        schedule();
}

void sched_thread_spawn(struct thread *thread)   /* Starts thread execution */
{
    thread->spawned = 1;
    arch_thread_spawn(thread);
}

int sched_running = 0;

void sched_init_spawn(struct proc *init)
{
    proc_init(init);

    /* init defaults */
    init->mask = 022;   /* umask */
    init->cwd = vfs_root;
    vfs_root->ref++;

    arch_sched_init();

    session_new(init);

    curthread = (struct thread *) init->threads.head->value;
    curthread->state = RUNNABLE;
    sched_running = 1;
    sched_thread_spawn(curthread);
}

void schedule() /* Called from arch-specific timer event handler */
{
    if (!ready_queue)    /* How did we even get here? */
        panic("Threads queue is not initialized");

    sched_urgent = 0;

    if (!curcpu()->idle)
        sched_thread_ready(curthread);

    curcpu()->idle = 0;

    if (!ready_queue->count) /* No ready threads, idle */
        kernel_idle();

    curthread = dequeue(ready_queue);
    curthread->sched_node = NULL;

    if (curthread->spawned) {
        arch_thread_switch(curthread);
    } else {
        sched_thread_spawn(curthread);
    }
}
