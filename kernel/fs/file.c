/*
 * Open files: what open(), pipe(), socket() and accept() make, and the
 * descriptors refer to (struct file, fs/vfs.h)
 *
 * An open file is one, whatever refers to it: descriptors that dup() and
 * fork() copied share it, its offset and status flags (POSIX's open file
 * description). It counts what refers to it, the descriptors and the
 * system calls using it: the last reference gone, the file is closed
 * (the filesystem's or the device's close, once) and freed.
 *
 * A system call holds what it uses (file_hold), until it returns: a close
 * meanwhile, by another thread, can't take the file away from under it.
 * The thread keeps the list, so a thread killed in the middle of a call
 * lets go of them too.
 */

#include <core/system.h>
#include <core/panic.h>
#include <fs/vfs.h>
#include <sys/proc.h>
#include <sys/thread.h>
#include <sys/sched.h>

MALLOC_DEFINE(M_FILE, "file", "open file");

struct file *file_new(void)
{
    struct file *file = kmalloc(sizeof(struct file), &M_FILE, M_ZERO);

    if (file)
        file->ref = 1;

    return file;
}

struct file *file_ref(struct file *file)
{
    file->ref++;
    return file;
}

int file_put(struct file *file)
{
    if (!file->ref)
        panic("file_put: a file no one refers to");

    if (--file->ref)
        return 0;

    /* a file set up halfway (its open failed) has nothing to close */
    int err = file->vnode ? vfs_file_close(file) : 0;

    kfree(file);
    return err;
}

struct file *file_hold(struct file *file)
{
    struct thread *thread = curthread;

    if (thread->files_held == THREAD_FILES_HELD)
        panic("file_hold: a system call holds too many files");

    thread->files[thread->files_held++] = file_ref(file);
    return file;
}

void thread_files_release(struct thread *thread)
{
    while (thread->files_held)
        file_put(thread->files[--thread->files_held]);
}
