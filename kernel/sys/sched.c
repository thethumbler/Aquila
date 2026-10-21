#include <core/system.h>
#include <core/panic.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/signal.h>
#include <ds/queue.h>
#include <dev/random.h>
#include <sys/callout.h>

#include <aquila/stat.h>

struct queue *ready_queue = QUEUE_NEW();   /* Ready threads queue */
struct thread *curthread = NULL;

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
    if (!kidle && curthread) {
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

            signal_proc_post(proc, SIGALRM);
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
 * `until' (0: no limit). Returns 1 if the time is up.
 */
int sched_wait(struct queue *queue, uint64_t until)
{
    if (until) {
        if (arch_rtime_ns() >= until)
            return 1;

        curthread->sleep_until = until;
        curthread->timer_node  = enqueue(timed, curthread);
    }

    thread_queue_sleep(queue);
    sched_timer_cancel(curthread);

    return until && arch_rtime_ns() >= until;
}

/*
 * Sleep until arch_rtime_ns() reaches `until'. A signal that has a
 * handler interrupts the sleep without returning here: the system call
 * fails with EINTR (see sched_sleep_interrupted()).
 */
void sched_sleep(uint64_t until)
{
    /* woken early by an ignored signal: keep sleeping */
    while (!sched_wait(naps, until))
        ;

    curthread->sleep_left = NULL;
}

/*
 * `thread' is about to run a signal handler instead of returning from a
 * system call, called with its address space active: give nanosleep()
 * the time it had left.
 */
void sched_sleep_interrupted(struct thread *thread)
{
    struct aquila_timespec *left = thread->sleep_left;

    if (!left)
        return;

    uint64_t now = arch_rtime_ns();
    uint64_t ns = now < thread->sleep_until ? thread->sleep_until - now : 0;

    left->tv_sec  = ns / 1000000000ULL;
    left->tv_nsec = ns % 1000000000ULL;
    thread->sleep_left = NULL;
}

void sched_thread_ready(struct thread *thread)
{
    struct qnode *sched_node = enqueue(ready_queue, thread);
    thread->sched_queue = ready_queue;
    thread->sched_node  = sched_node;
}

int kidle = 0;
void kernel_idle(void)
{
    kidle = 1;
    arch_idle();
}

/* an interrupt woke a thread up while the CPU was idle: it runs now rather
 * than at the next timer tick (a disk request would wait for one each) */
void sched_idle_wakeup(void)
{
    if (kidle && ready_queue->count)
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
    init->cwd = strdup("/");

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

    if (!kidle)
        sched_thread_ready(curthread);

    kidle = 0;

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
