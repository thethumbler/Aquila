#ifndef _SYS_UACCESS_H
#define _SYS_UACCESS_H

/*
 * User memory: what the kernel reads from it and writes to it goes
 * through copyin(), copyout() and copyinstr() (or an iobuf, fs/iobuf.h):
 * the range must be user space's [0, USER_STACK), and a fault the page
 * fault handler can't resolve (the memory isn't mapped, or not so) makes
 * them return -EFAULT. The kernel touching user memory otherwise, at an
 * address that turns out bad, is a bug: it panics.
 *
 * uaccess_check() is the range check alone.
 */

#include <core/system.h>
#include <core/arch.h>
#include <aquila/errno.h>

/** first address not accessible from user space */
#define UACCESS_LIMIT   ((uintptr_t) USER_STACK)

/**
 * check that [ptr, ptr+len) is a valid user range
 *
 * \return 0 if the range is valid, -EFAULT otherwise. A NULL pointer is
 * always rejected unless len is 0, callers that accept NULL must test
 * for it before calling.
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

/* the architecture's copies with fault recovery: what wasn't copied (0:
 * all of it); a string's length (max without a NUL), -EFAULT */
size_t arch_copy_user(void *dst, const void *src, size_t n);
long arch_copy_user_str(char *dst, const char *src, size_t max);

/** n bytes from user space's usrc into kdst: 0, -EFAULT */
static inline int copyin(void *kdst, const void *usrc, size_t n)
{
    if (uaccess_check(usrc, n))
        return -EFAULT;

    return n && arch_copy_user(kdst, usrc, n) ? -EFAULT : 0;
}

/** n bytes from ksrc to user space's udst: 0, -EFAULT */
static inline int copyout(void *udst, const void *ksrc, size_t n)
{
    if (uaccess_check(udst, n))
        return -EFAULT;

    return n && arch_copy_user(udst, ksrc, n) ? -EFAULT : 0;
}

/** as copyin() and copyout(), but how many bytes went before a fault (or
 * a range that isn't user space's): n if all of them */
static inline size_t copyin_upto(void *kdst, const void *usrc, size_t n)
{
    return uaccess_check(usrc, n) ? 0 : n - arch_copy_user(kdst, usrc, n);
}

static inline size_t copyout_upto(void *udst, const void *ksrc, size_t n)
{
    return uaccess_check(udst, n) ? 0 : n - arch_copy_user(udst, ksrc, n);
}

/** user space's string at usrc into kdst (max bytes, its NUL included):
 * its length, -EFAULT, -ENAMETOOLONG if it doesn't fit */
static inline long copyinstr(char *kdst, const char *usrc, size_t max)
{
    uintptr_t addr = (uintptr_t) usrc;

    if (!usrc || addr >= UACCESS_LIMIT || !max)
        return -EFAULT;

    /* not past user space */
    size_t room = MIN(max, UACCESS_LIMIT - addr);
    long len = arch_copy_user_str(kdst, usrc, room);

    if (len < 0)
        return len;

    if ((size_t) len == room)
        return room < max ? -EFAULT : -ENAMETOOLONG;

    return len;
}

#endif /* ! _SYS_UACCESS_H */
