#ifndef _SYS_UACCESS_H
#define _SYS_UACCESS_H

/*
 * User memory. What the kernel reads from it and writes to it goes
 * through copyin(), copyout() and copyinstr() (or an iobuf, fs/iobuf.h).
 * The range must be in user space, [0, USER_STACK), and a fault that the
 * page fault handler can't resolve (the memory isn't mapped, or not that
 * way) makes them return -EFAULT. The kernel touching user memory any
 * other way, at an address that turns out bad, is a bug: it panics.
 *
 * uaccess_check() is the range check alone.
 */

#include <core/system.h>
#include <core/arch.h>
#include <aquila/errno.h>

/* The first address that isn't in user space. */
#define UACCESS_LIMIT   ((uintptr_t) USER_STACK)

/**
 * Checks that [ptr, ptr + len) is in user space. Returns 0 if it is, or
 * -EFAULT. A NULL pointer is always rejected unless len is 0, so callers
 * that accept NULL must test for it first.
 */
static inline int uaccess_check(const void *ptr, size_t len)
{
    uintptr_t addr = (uintptr_t) ptr;

    if (!len)
        return 0;

    if (!ptr || addr >= UACCESS_LIMIT || len > UACCESS_LIMIT - addr)
        return -EFAULT;

    return 0;
}

/* The architecture's copies, which recover from faults. arch_copy_user()
 * returns how much wasn't copied (0: it all was); arch_copy_user_str()
 * returns the string's length (max if there was no NUL), or -EFAULT. */
size_t arch_copy_user(void *dst, const void *src, size_t n);
long arch_copy_user_str(char *dst, const char *src, size_t max);

/** Copies `n` bytes from user memory at `usrc` to `kdst`. Returns 0, or
 * -EFAULT. */
static inline int copyin(void *kdst, const void *usrc, size_t n)
{
    if (uaccess_check(usrc, n))
        return -EFAULT;

    return n && arch_copy_user(kdst, usrc, n) ? -EFAULT : 0;
}

/** Copies `n` bytes from `ksrc` to user memory at `udst`. Returns 0, or
 * -EFAULT. */
static inline int copyout(void *udst, const void *ksrc, size_t n)
{
    if (uaccess_check(udst, n))
        return -EFAULT;

    return n && arch_copy_user(udst, ksrc, n) ? -EFAULT : 0;
}

/** Copies as copyin() does, but returns how many bytes were copied before
 * a fault (0 for a range that isn't in user space): `n` if they all were. */
static inline size_t copyin_upto(void *kdst, const void *usrc, size_t n)
{
    return uaccess_check(usrc, n) ? 0 : n - arch_copy_user(kdst, usrc, n);
}

/** Copies as copyout() does, but returns how many bytes were copied
 * before a fault: `n` if they all were. */
static inline size_t copyout_upto(void *udst, const void *ksrc, size_t n)
{
    return uaccess_check(udst, n) ? 0 : n - arch_copy_user(udst, ksrc, n);
}

/** Copies a string from user memory at `usrc` to `kdst` (at most `max`
 * bytes, its NUL included). Returns its length, -EFAULT, or -ENAMETOOLONG
 * if it doesn't fit. */
static inline long copyinstr(char *kdst, const char *usrc, size_t max)
{
    uintptr_t addr = (uintptr_t) usrc;

    if (!usrc || addr >= UACCESS_LIMIT || !max)
        return -EFAULT;

    /* don't read past the end of user space */
    size_t room = MIN(max, UACCESS_LIMIT - addr);
    long len = arch_copy_user_str(kdst, usrc, room);

    if (len < 0)
        return len;

    if ((size_t) len == room)
        return room < max ? -EFAULT : -ENAMETOOLONG;

    return len;
}

#endif /* ! _SYS_UACCESS_H */
