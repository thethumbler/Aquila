#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/binfmt.h>
#include <sys/uaccess.h>
#include <mm/mm.h>
#include <aquila/fcntl.h>

/* maximum total size of arguments and environment (strings + pointers) */
#define EXEC_ARG_MAX    (128 * 1024)

static void exec_strings_free(char **strs, int count)
{
    if (!strs)
        return;

    for (int i = 0; i < count; ++i)
        kfree(strs[i]);

    kfree(strs);
}

/*
 * A NULL-terminated user array of user strings into a NULL-terminated
 * kernel array of kernel copies (*count of them), their size (strings and
 * pointers) added to `total', no more than EXEC_ARG_MAX in all; each
 * string comes in through `buf' (EXEC_ARG_MAX bytes).
 */
static int exec_strings_get(char * const u_strs[], char *buf, size_t *total, int *count, char ***ref)
{
    int cnt = 0, err = 0;
    char *u;

    /* how many */
    for (; u_strs; ++cnt) {
        if (copyin(&u, &u_strs[cnt], sizeof(u)))
            return -EFAULT;

        if (!u)
            break;

        if ((cnt + 1) * sizeof(char *) > EXEC_ARG_MAX)
            return -E2BIG;
    }

    char **strs = kmalloc((cnt + 1) * sizeof(char *), &M_BUFFER, M_ZERO);

    if (!strs)
        return -ENOMEM;

    for (int i = 0; i < cnt; ++i) {
        size_t room = EXEC_ARG_MAX - *total;
        long len;

        /* the array may have changed since it was counted */
        if (copyin(&u, &u_strs[i], sizeof(u)) || !u) {
            err = -EFAULT;
            goto error;
        }

        if (!room || (len = copyinstr(buf, u, room)) == -ENAMETOOLONG) {
            err = -E2BIG;
            goto error;
        }

        if (len < 0) {
            err = len;
            goto error;
        }

        *total += len + 1 + sizeof(char *);

        if (*total > EXEC_ARG_MAX) {
            err = -E2BIG;
            goto error;
        }

        if (!(strs[i] = kmalloc(len + 1, &M_BUFFER, 0))) {
            err = -ENOMEM;
            goto error;
        }

        memcpy(strs[i], buf, len + 1);
    }

    *count = cnt;
    *ref = strs;
    return 0;

error:
    exec_strings_free(strs, cnt);
    return err;
}

/* how many interpreters may be scripts themselves */
#define EXEC_SCRIPT_DEPTH   4

/*
 * The interpreter of a script, "#!interpreter [argument]" on its first
 * line, and the argument (the rest of the line), NULL if it isn't one.
 */
static int exec_script(struct proc *proc, const char *path, char **interp, char **arg)
{
    struct uio uio = PROC_UIO(proc);
    struct vnode *vnode = NULL;
    char buf[128];
    int err;

    *interp = *arg = NULL;

    if ((err = vfs_lookup(path, &uio, &vnode, NULL)))
        return err;

    /* a regular file the process may execute, as any program */
    if (!S_ISREG(vnode->mode))
        err = -EACCES;
    else
        err = vfs_access(vnode, &uio, VFS_X);

    ssize_t len = err ? 0 : vfs_read(vnode, 0, sizeof(buf) - 1, buf);
    vfs_close(vnode);

    if (err)
        return err;

    if (len < 2 || buf[0] != '#' || buf[1] != '!')
        return 0;

    buf[len] = '\0';

    char *end = buf + 2;
    while (*end && *end != '\n')
        ++end;

    /* the whole line has to fit */
    if (!*end)
        return -ENOEXEC;

    *end = '\0';

    char *p = buf + 2;
    while (*p == ' ' || *p == '\t')
        ++p;

    char *name = p;
    while (*p && *p != ' ' && *p != '\t')
        ++p;

    if (p == name)
        return -ENOEXEC;

    if (*p)
        *p++ = '\0';

    while (*p == ' ' || *p == '\t')
        ++p;

    /* trailing blanks aren't part of the argument */
    while (end > p && (end[-1] == ' ' || end[-1] == '\t'))
        *--end = '\0';

    if (!(*interp = strdup(name)))
        return -ENOMEM;

    if (*p && !(*arg = strdup(p))) {
        kfree(*interp);
        *interp = NULL;
        return -ENOMEM;
    }

    return 0;
}

/*
 * A script runs as its interpreter, with the arguments: the interpreter,
 * its argument from the script if any, the script's path, and those of
 * the script but the first (argv[0]).
 */
static int exec_script_args(char *interp, char *arg, const char *path, char ***argp, int *argc)
{
    int count = 1 + !!arg + 1 + (*argc > 1 ? *argc - 1 : 0);
    char **args = kmalloc((count + 1) * sizeof(char *), &M_BUFFER, M_ZERO);
    char *script = strdup(path);

    if (!args || !script) {
        kfree(args);
        kfree(script);
        return -ENOMEM;
    }

    int i = 0;
    args[i++] = interp;
    if (arg)
        args[i++] = arg;
    args[i++] = script;

    for (int j = 1; j < *argc; ++j)
        args[i++] = (*argp)[j];

    if (*argc)
        kfree((*argp)[0]);
    kfree(*argp);

    *argp = args;
    *argc = count;
    return 0;
}

int proc_execve(struct thread *thread, const char *fn, char * const _argp[], char * const _envp[])
{
    int err = 0;
    struct proc *proc = thread->owner;

    int argc = 0, envc = 0;
    size_t total = 0;

    char **argp = NULL;
    char **envp = NULL;

    /* the arguments and the environment: the kernel's copies */
    char *buf = kmalloc(EXEC_ARG_MAX, &M_BUFFER, 0);

    if (!buf)
        return -ENOMEM;

    if (!(err = exec_strings_get(_argp, buf, &total, &argc, &argp)))
        err = exec_strings_get(_envp, buf, &total, &envc, &envp);

    kfree(buf);

    if (err)
        goto free_resources;

    /* scripts run as their interpreters */
    for (int depth = 0;; ++depth) {
        char *interp, *arg;

        if ((err = exec_script(proc, fn, &interp, &arg)))
            goto free_resources;

        if (!interp)
            break;

        if (depth == EXEC_SCRIPT_DEPTH ||
            (err = exec_script_args(interp, arg, fn, &argp, &argc))) {
            kfree(interp);
            kfree(arg);
            err = err ? err : -ELOOP;
            goto free_resources;
        }

        fn = interp;
    }

    if ((err = binfmt_load(proc, fn, NULL))) {
        if (!proc->vm_space.vm_entries.count) {
            /* the failure happened after the old image was destroyed,
             * there is nothing to return to, kill the process */
            exec_strings_free(argp, argc);
            exec_strings_free(envp, envc);

            proc->exit = PROC_EXIT(SIGSEGV, SIGSEGV);
            proc_kill(proc);
            arch_sleep();
        }

        goto free_resources;
    }

    thread->spawned = 0;

    /* close descriptors marked close-on-exec */
    for (int fd = 0; fd < FDS_COUNT; ++fd)
        if (proc_fd_file(proc, fd) && (proc->fds[fd].flags & FD_CLOEXEC))
            proc_fd_close(proc, fd);

    /* caught signals go back to their default action, ignored ones stay
     * ignored (POSIX); the mask and the pending ones stay */
    for (int i = 0; i <= SIG_MAX; ++i) {
        if (proc->sigaction[i].sa_handler != SIG_IGN)
            proc->sigaction[i].sa_handler = SIG_DFL;

        proc->sigaction[i].sa_mask = 0;
        proc->sigaction[i].sa_flags = 0;
    }

    arch_sys_execve(proc, argc + 1, argp, envc + 1, envp);

free_resources:
    exec_strings_free(argp, argc);
    exec_strings_free(envp, envc);

    return err;
}

int thread_execve(struct thread *thread, char * const _argp[], char * const _envp[])
{
    struct proc *proc = thread->owner;

    char **argp = (char **) _argp;
    char **envp = (char **) _envp;

    /* Start at the top of user stack */
    char *stack = (char *) thread->stack_base;

    int argc = 0, envc = 0;
    
    if (envp)
        for (char **env_p = envp; *env_p; ++env_p)
            ++envc;

    if (argp)
        for (char **arg_p = argp; *arg_p; ++arg_p)
            ++argc;

    char *u_envp[envc+1];
    char *u_argp[argc+1];

    /* TODO support upward growing stacks */

    /* Push envp strings */
    u_envp[envc] = NULL;
    for (int i = envc - 1; i >= 0; --i) {
        stack -= strlen(envp[i]) + 1;
        strcpy((char *) stack, envp[i]);
        u_envp[i] = (char *) stack;
    }

    /* Push argp strings */
    u_argp[argc] = NULL;
    for (int i = argc - 1; i >= 0; --i) {
        stack -= strlen(argp[i]) + 1;
        strcpy((char *) stack, argp[i]);
        u_argp[i] = (char *) stack;
    }

    /* Push envp array */
    stack -= (envc+1) * sizeof(char *);
    memcpy((void *) stack, u_envp, (envc+1) * sizeof(char *));

    uintptr_t env_ptr = (uintptr_t) stack;

    stack -= (argc+1) * sizeof(char *);
    memcpy((void *) stack, u_argp, (argc+1) * sizeof(char *));

    uintptr_t arg_ptr = (uintptr_t) stack;

    /* main(int argc, char **argv, char **envp) */
    stack -= sizeof(uintptr_t);
    *(uintptr_t *) stack = env_ptr;

    stack -= sizeof(uintptr_t);
    *(uintptr_t *) stack = arg_ptr;

#if ARCH_BITS==32
    stack -= sizeof(int32_t);
    *(int32_t *) stack = argc;
#else
    stack -= sizeof(int64_t);
    *(int64_t *) stack = argc;
#endif

    thread->stack = (uintptr_t) stack;
    
    return 0;
}
