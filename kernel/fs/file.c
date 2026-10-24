/** \file
 * Open files (`struct file`, in `fs/vfs.h`): what `open()`, `pipe()`,
 * `socket()` and `accept()` make, and what descriptors refer to.
 *
 * An open file is shared by everything that refers to it: descriptors
 * that `dup()` and `fork()` copied share it, with its offset and status
 * flags (what POSIX calls an open file description). It counts its
 * references, from descriptors and from system calls using it. When the
 * last one goes, the file is closed (the filesystem's or the device's
 * close runs once) and freed.
 *
 * A system call holds the files it uses (`file_hold()`) until it
 * returns, so that a `close()` by another thread meanwhile can't free a
 * file under it. The thread keeps the list, so a thread killed in the
 * middle of a call lets go of them too.
 */

#include <core/system.h>
#include <core/panic.h>
#include <fs/vfs.h>
#include <sys/proc.h>
#include <sys/thread.h>
#include <sys/sched.h>

MALLOC_DEFINE(M_FILE, "file", "open file");

/**
 * Makes a new open file, with one reference.
 *
 * @return the file, or NULL if there was no memory
 */
struct file *file_new(void)
{
    struct file *file = kmalloc(sizeof(struct file), &M_FILE, M_ZERO);

    if (file)
        file->ref = 1;

    return file;
}

/**
 * Adds a reference to an open file.
 *
 * @param file  the file
 * @return `file`
 */
struct file *file_ref(struct file *file)
{
    file->ref++;
    return file;
}

/**
 * Drops a reference to an open file. The last one closes and frees it.
 *
 * @param file  the file
 * @return 0, or what closing the file returned
 */
int file_put(struct file *file)
{
    if (!file->ref)
        panic("file_put: a file no one refers to");

    if (--file->ref)
        return 0;

    /* a file that was set up only halfway (its open failed) has
     * nothing to close */
    int err = file->vnode ? vfs_file_close(file) : 0;

    kfree(file);
    return err;
}

/**
 * Holds an open file for the current system call, until it returns.
 *
 * @param file  the file
 * @return `file`
 */
struct file *file_hold(struct file *file)
{
    struct thread *thread = curthread;

    if (thread->files_held == THREAD_FILES_HELD)
        panic("file_hold: a system call holds too many files");

    thread->files[thread->files_held++] = file_ref(file);
    return file;
}

/**
 * Lets go of the open files a thread's system call held, when the call
 * returns or the thread is killed.
 *
 * @param thread  the thread
 */
void thread_files_release(struct thread *thread)
{
    while (thread->files_held)
        file_put(thread->files[--thread->files_held]);
}
