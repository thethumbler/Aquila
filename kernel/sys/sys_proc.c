/** \file
 * System calls: processes and threads: exit, exec, fork, wait, their IDs
 * and groups, memory (sbrk, mmap), the machine (uname, reboot)
 */

#include "syscalls.h"

long sys_exit(int code)
{
    syscall_log(LOG_DEBUG, "exit(code=%d)\n", code);

    struct proc *owner = curproc;

    owner->exit = PROC_EXIT(code, 0);  /* Child exited normally */

    proc_kill(owner);
    arch_sleep();

    /* not reached: the process is gone, this thread with it */
    for (;;);
    return 0;
}

long sys_execve(const char *path, char * const argp[], char * const envp[])
{
    syscall_log(LOG_DEBUG, "execve(path=%p, argp=%p, envp=%p)\n", path, argp, envp);

    char *fn;
    int err = upath_get(path, &fn);

    if (err) {
        return err;
    }

    if (!*fn) {
        kfree(fn);
        return -ENOENT;
    }

    err = proc_execve(curthread, fn, argp, envp);

    kfree(fn);

    if (err)
        return err;

    /* the new program, from its entry point: no return from here */
    sched_thread_spawn(curthread);
    return 0;
}

long sys_fork(void)
{
    syscall_log(LOG_DEBUG, "fork()\n");

    struct proc *fork = NULL;
    int err = proc_fork(curthread, &fork);

    if (err) {
        return err;
    }

    /* the child runs, from fork()'s return (0: proc_fork()) */
    struct thread *thread = (struct thread *) fork->threads.head->value;
    sched_thread_ready(thread);

    return fork->pid;
}

long sys_getpid(void)
{
    syscall_log(LOG_DEBUG, "getpid()\n");
    return curproc->pid;
}

long sys_sbrk(ptrdiff_t incr)
{
    syscall_log(LOG_DEBUG, "sbrk(incr=0x%x)\n", incr);

    uintptr_t heap_start = curproc->heap_start;
    uintptr_t heap = curproc->heap;

    /* the heap may grow up to the next mapped region (or the stack) */
    uintptr_t heap_limit = USER_STACK_BASE;
    struct qnode *next = curproc->heap_vm->qnode->next;

    if (next)
        heap_limit = ((struct vm_entry *) next->value)->base;

    if ((incr < 0 && (uintptr_t) -incr > heap - heap_start) ||
        (incr > 0 && (heap_limit < heap || (uintptr_t) incr > heap_limit - heap))) {
        /* libc passes the return value through and only treats
         * (void *) -1 as failure, so return that instead of -ENOMEM */
        return (uintptr_t) -1;
    }

    curproc->heap = heap + incr;
    curproc->heap_vm->size = PAGE_ROUND(heap + incr - heap_start);

    return heap;
}

long sys_times(struct aquila_tms *buf)
{
    syscall_log(LOG_DEBUG, "times(buf=%p)\n", buf);

    const uint64_t tick = 1000000000ULL / AQUILA_CLK_TCK;   /* ns */

    struct aquila_tms tms = {
        .tms_utime  = curproc->utime / tick,
        .tms_stime  = curproc->stime / tick,
        .tms_cutime = curproc->cutime / tick,
        .tms_cstime = curproc->cstime / tick,
    };

    if (copyout(buf, &tms, sizeof(tms))) {
        return -EFAULT;
    }

    /* elapsed real time, since boot */
    return (uint32_t) (arch_rtime_ns() / tick);
}

long sys_waitpid(int pid, int *stat_loc, int options)
{
    syscall_log(LOG_DEBUG, "waitpid(pid=%d, stat_loc=%p, options=0x%x)\n", pid, stat_loc, options);

    /* pid > 0:   that child
     * pid == -1: any child
     * pid == 0:  any child in the caller's process group
     * pid < -1:  any child in the process group -pid */
    pid_t pgid = pid == 0 ? curproc->pgrp->pgid : -pid;

    for (;;) {
        int found = 0;

        queue_for (node, procs) {
            struct proc *proc = node->value;
            int status;

            if (proc->parent != curproc)
                continue;

            if (pid > 0 ? proc->pid != pid : pid != -1 && proc->pgrp->pgid != pgid)
                continue;

            found = 1;

            if (proc->zombie) {
                status = proc->exit;
            } else if ((options & WUNTRACED) && (proc->wait_event & WAIT_STOPPED)) {
                proc->wait_event &= ~WAIT_STOPPED;
                status = (proc->stopped << 8) | 0x7f;
            } else if ((options & WCONTINUED) && (proc->wait_event & WAIT_CONTINUED)) {
                proc->wait_event &= ~WAIT_CONTINUED;
                status = 0xffff;
            } else {
                continue;
            }

            /* nowhere to put it: the child stays as it is */
            if (stat_loc && copyout(stat_loc, &status, sizeof(status))) {
                return -EFAULT;
            }

            pid_t child = proc->pid;

            if (proc->zombie)
                proc_reap(proc);

            return child;
        }

        if (!found) {
            return -ECHILD;
        }

        if (options & WNOHANG) {
            return 0;
        }

        if (waitq_sleep(&curproc->wait_queue)) {
            return -ERESTARTSYS;
        }
    }
}

/* set by sethostname(), shown by uname() */
static char nodename[sizeof(((struct utsname *) 0)->nodename)] = UTSNAME_NODENAME;

long sys_uname(struct utsname *name)
{
    syscall_log(LOG_DEBUG, "uname(name=%p)\n", name);

    struct utsname u;
    memset(&u, 0, sizeof(u));

    strcpy(u.sysname,  UTSNAME_SYSNAME);
    strcpy(u.nodename, nodename);
    strcpy(u.release,  UTSNAME_RELEASE);
    /* the compiler's version string may be long */
    size_t n = MIN(sizeof(u.version) - 1, (size_t) strlen(UTSNAME_VERSION));
    memcpy(u.version, UTSNAME_VERSION, n);
    strcpy(u.machine,  UTSNAME_MACHINE);

    return copyout(name, &u, sizeof(u));
}

long sys_thread_create(struct aquila_thread_args *__uthread)
{
    struct aquila_thread_args a;

    if (copyin(&a, __uthread, sizeof(a))) {
        return -EFAULT;
    }

    syscall_log(LOG_DEBUG,
            "thread_create(stack=%p, entry=%p, uentry=%p, arg=%p, attr=%p)\n",
            a.stack, a.entry, a.uentry, a.arg, a.attr);

    uintptr_t stack = a.stack;

    /* the kernel pushes the initial frame (3 words) on the user stack */
    if (stack < 3 * sizeof(uintptr_t) ||
        uaccess_check((void *) (stack - 3 * sizeof(uintptr_t)), 3 * sizeof(uintptr_t))) {
        return -EFAULT;
    }

    int err = 0;
    struct thread *thread = NULL;

    if ((err = thread_create(curthread, stack, a.entry, a.uentry, a.arg, a.attr, &thread))) {
        return err;
    }

    sched_thread_ready(thread);
    return thread->tid;
}

long sys_thread_exit(void *value_ptr)
{
    syscall_log(LOG_DEBUG, "thread_exit(value_ptr=%p)\n", value_ptr);

    curthread->exit_value = value_ptr;
    struct proc *owner = curproc;

    /* last live thread exiting terminates the process */
    int alive = 0;
    queue_for (node, &owner->threads) {
        struct thread *thread = node->value;
        if (thread != curthread && thread->state != ZOMBIE)
            ++alive;
    }

    if (!alive)
        sys_exit(0);

    /* Wakeup owner if it is waiting for joining */
    waitq_wakeup(&owner->thread_join);

    /* The thread stays in owner->threads as a zombie, freed by
     * thread_join, or when the process exits. Its kernel stack (which we
     * are running on) is released from the idle stack by
     * arch_cur_thread_kill */
    curthread->state = ZOMBIE;
    arch_cur_thread_kill();

    /* not reached: the thread is gone */
    for (;;);
    return 0;
}

long sys_thread_join(int tid, void **value_ptr)
{
    syscall_log(LOG_DEBUG, "thread_join(tid=%d, value_ptr=%p)\n", tid, value_ptr);

    struct proc *owner = curproc;
    struct thread *thread = NULL;

    queue_for (node, &owner->threads) {
        struct thread *_thread = node->value;
        if (_thread->tid == tid)
            thread = _thread;
    }

    /* No such thread (or joined already) */
    if (!thread) {
        return -ESRCH;
    }

    /* it would wait for itself forever */
    if (thread == curthread) {
        return -EDEADLK;
    }

    while (thread->state != ZOMBIE) {
        if (waitq_sleep(&curproc->thread_join)) {
            return -ERESTARTSYS;
        }
    }

    if (value_ptr && copyout(value_ptr, &thread->exit_value, sizeof(void *))) {
        return -EFAULT;
    }

    /* joined: what's left of it goes (thread_exit freed the rest; its
     * CPU is done with it, it let go of the kernel lock since) */
    int joined = thread->tid;
    queue_remove(&owner->threads, thread);
    kfree(thread);

    return joined;
}

long sys_setpgid(pid_t pid, pid_t pgid)
{
    syscall_log(LOG_DEBUG, "setpgid(pid=%d, pgid=%d)\n", pid, pgid);

    if (pgid < 0) {
        return -EINVAL;
    }

    /* the caller or one of its children */
    struct proc *proc = pid ? proc_pid_find(pid) : curproc;

    if (!proc || (proc != curproc && proc->parent != curproc)) {
        return -ESRCH;
    }

    struct session *session = proc->pgrp->session;

    /* a session leader can't change its group, nor can a process outside
     * the caller's session */
    if (session != curproc->pgrp->session || session->leader == proc) {
        return -EPERM;
    }

    return proc_setpgid(proc, pgid ? pgid : proc->pid);
}

/**
 * There are no separate effective or saved ids, setuid() and setgid()
 * set the process ids: root may set any id, other users only their own.
 */
long sys_setuid(uid_t uid)
{
    syscall_log(LOG_DEBUG, "setuid(uid=%d)\n", uid);

    if (curproc->uid != 0 && uid != curproc->uid) {
        return -EPERM;
    }

    curproc->uid = uid;
    return 0;
}

long sys_setgid(gid_t gid)
{
    syscall_log(LOG_DEBUG, "setgid(gid=%d)\n", gid);

    if (curproc->uid != 0 && gid != curproc->gid) {
        return -EPERM;
    }

    curproc->gid = gid;
    return 0;
}

long sys_getuid(void)
{
    syscall_log(LOG_DEBUG, "getuid()\n");
    return curproc->uid;
}

long sys_getgid(void)
{
    syscall_log(LOG_DEBUG, "getgid()\n");
    return curproc->gid;
}

long sys_mmap(struct aquila_mmap_args *uargs, void **ret)
{
    struct aquila_mmap_args _args, *args = &_args;
    void *none = NULL;

    /* the address goes out at the end: where to, writable, first */
    if (copyin(args, uargs, sizeof(*args)) || copyout(ret, &none, sizeof(none))) {
        return -EFAULT;
    }

    syscall_log(LOG_DEBUG, "mmap(addr=%p, len=%d, prot=%x, flags=%x, fildes=%d, off=%d, ret=%p)\n",
            args->addr, args->len, args->prot, args->flags, args->fildes, args->off, ret);

    int err = 0;
    int anon = args->flags & MAP_ANONYMOUS;

    /* anonymous memory is the process's own (fork() copies it) */
    if (anon && (args->flags & MAP_SHARED)) {
        return -EINVAL;
    }

    struct file *file = anon ? NULL : fd_file(args->fildes);

    if (!anon && !file) {    /* Invalid File Descriptor */
        return -EBADF;
    }

    if (file && (file->flags & FILE_SOCKET)) {
        return -ENODEV;
    }

    if (!args->len) {
        return -EINVAL;
    }

    /* a fixed mapping must lie entirely in user space, on pages */
    if ((args->flags & MAP_FIXED) && (((uintptr_t) args->addr & PAGE_MASK) ||
                uaccess_check(args->addr, args->len))) {
        return -EINVAL;
    }

    /* Allocate VMR */
    struct vm_entry *vm_entry;
    vm_entry = vm_entry_new();

    if (vm_entry == NULL) {
        err = -ENOMEM;
        goto error;
    }

    /* Initialize VM entry */
    vm_entry->base   = (uintptr_t) args->addr;
    vm_entry->size   = PAGE_ROUND(args->len);
    vm_entry->flags  = args->prot & PROT_READ  ? VM_UR : 0;
    vm_entry->flags |= args->prot & PROT_WRITE ? VM_UW : 0;
    vm_entry->flags |= args->prot & PROT_EXEC  ? VM_UX : 0;
    vm_entry->flags |= args->flags & MAP_SHARED ? VM_SHARED : 0;
    vm_entry->off    = anon ? 0 : args->off;

    /* without an object, pages are zero-filled when first used */
    vm_entry->vm_object = anon ? NULL : vm_object_vnode(file->vnode);

    if (!anon && !vm_entry->vm_object) {
        err = -ENOMEM;
        goto error;
    }

    if (vm_entry->vm_object)
        vm_object_incref(vm_entry->vm_object);

    if (!(args->flags & MAP_FIXED))
        vm_entry->base = 0;  /* Allocate memory region */

    struct vm_space *vm_space = &curproc->vm_space;
    if ((err = vm_space_insert(vm_space, vm_entry)))
        goto error;

    if (!anon && !(args->flags & MAP_PRIVATE) && (err = vfs_map(vm_space, vm_entry)))
        goto error;

    void *addr = (void *) vm_entry->base;
    err = copyout(ret, &addr, sizeof(addr));

    return err;

error:
    if (vm_entry) {
        if (vm_entry->qnode)
            queue_node_remove(&curproc->vm_space.vm_entries, vm_entry->qnode);

        if (vm_entry->vm_object)
            vm_object_decref(vm_entry->vm_object);

        kfree(vm_entry);
    }

    return err;
}

long sys_munmap(void *addr, size_t len)
{
    syscall_log(LOG_DEBUG, "munmap(addr=%p, len=%d)\n", addr, len);

    struct vm_space *vm_space = &curproc->vm_space;

    if (!len) {
        return -EINVAL;
    }

    queue_for (node, &vm_space->vm_entries) {
        struct vm_entry *vm_entry = node->value;

        /* heap and stack are managed by the kernel (sbrk/exec) */
        if (vm_entry == curproc->heap_vm || vm_entry == curproc->stack_vm)
            continue;

        if (vm_entry->base == (uintptr_t) addr && vm_entry->size == PAGE_ROUND(len)) {
            queue_node_remove(&vm_space->vm_entries, vm_entry->qnode);
            vm_unmap_full(vm_space, vm_entry);
            vm_entry_destroy(vm_entry);
            kfree(vm_entry);
            return 0;
        }
    }

    /* Not found */
    return -EINVAL;
}

long sys_sysconf(int name)
{
    syscall_log(LOG_DEBUG, "sysconf(name=%d)\n", name);
    return -ENOSYS;
}

long sys_getppid(void)
{
    syscall_log(LOG_DEBUG, "getppid()\n");

    /* orphans are init's; init and the kernel's process have no parent */
    return curproc->parent ? curproc->parent->pid : 0;
}

long sys_getpgrp(void)
{
    syscall_log(LOG_DEBUG, "getpgrp()\n");
    return curproc->pgrp->pgid;
}

/** random bytes, as many as asked (getentropy) */
long sys_getrandom(void *buf, size_t len, unsigned flags __unused)
{
    if (uaccess_check(buf, len)) {
        return -EFAULT;
    }

    /* a piece at a time through the kernel */
    uint8_t kbuf[256];
    size_t done = 0;

    while (done < len) {
        size_t n = MIN(len - done, sizeof(kbuf));
        random_get(kbuf, n);

        if (copyout((char *) buf + done, kbuf, n))
            break;

        done += n;
    }

    memset(kbuf, 0, sizeof(kbuf));
    return done || !len ? (ssize_t) done : -EFAULT;
}

/** the calling thread's id (pthread_self) */
long sys_thread_self(void)
{
    return curthread->tid;
}

long sys_reboot(unsigned cmd)
{
    syscall_log(LOG_DEBUG, "reboot(cmd=0x%x)\n", cmd);

    if (curproc->uid) {
        return -EPERM;
    }

    return kernel_reboot(cmd);
}

long sys_setsid(void)
{
    syscall_log(LOG_DEBUG, "setsid()\n");

    struct proc *proc = curproc;
    struct pgroup *pgrp = proc->pgrp;
    struct qnode *pgrp_node = proc->pgrp_node;

    /* a group leader can't leave its group, the group would be in two sessions */
    if (pgrp->pgid == proc->pid) {
        return -EPERM;
    }

    int ret = session_new(proc);

    if (ret) {
        proc->pgrp = pgrp;
        proc->pgrp_node = pgrp_node;
        return ret;
    }

    queue_node_remove(pgrp->procs, pgrp_node);
    pgrp_release(pgrp);
    return proc->pid;
}

long sys_sethostname(const char *name, size_t len)
{
    syscall_log(LOG_DEBUG, "sethostname(name=%p, len=%d)\n", name, len);

    if (curproc->uid != 0) {
        return -EPERM;
    }

    if (len >= sizeof(nodename)) {
        return -EINVAL;
    }

    char name_in[sizeof(nodename)];

    if (copyin(name_in, name, len)) {
        return -EFAULT;
    }

    memcpy(nodename, name_in, len);
    nodename[len] = '\0';
    return 0;
}
