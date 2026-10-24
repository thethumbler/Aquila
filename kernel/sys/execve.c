/** \file
 * `execve()`: a process runs another program.
 *
 * The arguments and the environment are copied into the kernel first,
 * since the process's memory is about to go. A script ("#!" on its first
 * line) runs as its interpreter. Then the program is loaded
 * (`binfmt_load()`), the close-on-exec descriptors are closed, caught
 * signals go back to their default action, and the architecture starts
 * the program with its arguments on the stack (`arch_sys_execve()`).
 */

#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/binfmt.h>
#include <sys/uaccess.h>
#include <mm/mm.h>
#include <aquila/fcntl.h>

/* the most that the arguments and the environment may take, strings and
 * pointers together */
#define EXEC_ARG_MAX    (128 * 1024)

/** Frees an array of `count` strings that `exec_strings_get()` made. */
static void exec_strings_free(char **strs, int count)
{
    if (!strs)
        return;

    for (int i = 0; i < count; ++i)
        kfree(strs[i]);

    kfree(strs);
}

/**
 * Copies a NULL-terminated array of strings (the arguments or the
 * environment) from user memory into a NULL-terminated array of kernel
 * copies, setting `*count` to how many there are and `*ref` to the array.
 * Their size, strings and pointers, is added to `*total`, which may not
 * go past `EXEC_ARG_MAX`. Each string comes in through `buf`, of
 * `EXEC_ARG_MAX` bytes. Returns 0, or `-EFAULT`, `-E2BIG` or `-ENOMEM`.
 */
static int exec_strings_get(char * const u_strs[], char *buf, size_t *total, int *count, char ***ref)
{
    int cnt = 0, err = 0;
    char *u;

    /* count the strings */
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

/* how many interpreters in a row may be scripts themselves */
#define EXEC_SCRIPT_DEPTH   4

/**
 * Reads the interpreter of a script, from "#!interpreter [argument]" on
 * its first line. Sets `*interp` to the interpreter and `*arg` to the
 * argument (the rest of the line), or to NULL if the file isn't a script
 * or there is no argument. Returns 0 or a negative errno.
 */
static int exec_script(struct proc *proc, const char *path, char **interp, char **arg)
{
    struct uio uio = PROC_UIO(proc);
    struct vnode *vnode = NULL;
    char buf[128];
    int err;

    *interp = *arg = NULL;

    if ((err = vfs_lookup(path, &uio, &vnode)))
        return err;

    /* like any program, it must be a regular file that the process may
     * execute */
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

    /* the whole line has to fit in the buffer */
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

/**
 * Makes the arguments of a script's interpreter: the interpreter, its
 * argument from the script if there is one, the script's path, then the
 * script's own arguments but the first (`argv[0]`). They replace `*argp`
 * and `*argc`. Returns 0 or `-ENOMEM`.
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

/**
 * Runs another program in a thread's process, for `execve()`. On success,
 * the thread starts the new program when it returns to user mode.
 *
 * @param thread  the thread calling `execve()`
 * @param fn      the program's path
 * @param _argp   the arguments, in user memory
 * @param _envp   the environment, in user memory
 * @return 0, or a negative errno: `-EFAULT`, `-E2BIG`, `-ENOMEM`,
 *         `-ELOOP` (too many nested scripts), or the loader's. If loading
 *         fails after the old program is gone, the process is killed
 *         (`SIGSEGV`) instead.
 */
int proc_execve(struct thread *thread, const char *fn, char * const _argp[], char * const _envp[])
{
    int err = 0;
    struct proc *proc = thread->owner;

    int argc = 0, envc = 0;
    size_t total = 0;

    char **argp = NULL;
    char **envp = NULL;

    /* copy the arguments and the environment into the kernel */
    char *buf = kmalloc(EXEC_ARG_MAX, &M_BUFFER, 0);

    if (!buf)
        return -ENOMEM;

    if (!(err = exec_strings_get(_argp, buf, &total, &argc, &argp)))
        err = exec_strings_get(_envp, buf, &total, &envc, &envp);

    kfree(buf);

    if (err)
        goto free_resources;

    /* a script runs as its interpreter */
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
            /* the failure came after the old program was destroyed:
             * there is nothing to return to, so the process is killed */
            exec_strings_free(argp, argc);
            exec_strings_free(envp, envc);

            proc->exit = PROC_EXIT(SIGSEGV, SIGSEGV);
            proc_kill(proc);
            arch_sleep();
        }

        goto free_resources;
    }

    thread->spawned = 0;

    /* close the descriptors marked close-on-exec */
    for (int fd = 0; fd < FDS_COUNT; ++fd)
        if (proc_fd_file(proc, fd) && (proc->fds[fd].flags & FD_CLOEXEC))
            proc_fd_close(proc, fd);

    /* caught signals go back to their default action, and ignored ones
     * stay ignored (POSIX); the signal mask and the pending signals stay */
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
