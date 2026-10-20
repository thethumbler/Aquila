#ifndef _SYS_UACCESS_H
#define _SYS_UACCESS_H

/*
 * Helpers for validating pointers passed from user space.
 *
 * The kernel accesses user memory directly, so any pointer coming from
 * user space must be checked before it is dereferenced, otherwise a user
 * process can make the kernel read or write kernel memory.  These helpers
 * only check that the range lies entirely inside the user part of the
 * address space [0, USER_STACK); faults on unmapped user addresses are
 * handled by the page fault handler (which signals the process).
 */

#include <core/system.h>
#include <core/arch.h>
#include <bits/errno.h>

/** first address not accessible from user space */
#define UACCESS_LIMIT   ((uintptr_t) USER_STACK)

/**
 * \ingroup sys
 * \brief check that [ptr, ptr+len) is a valid user range
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

/**
 * \ingroup sys
 * \brief check that a NUL-terminated string lies entirely in user space
 *
 * \return 0 if the string is valid, -EFAULT otherwise.
 */
static inline int uaccess_str_check(const char *str)
{
    uintptr_t addr = (uintptr_t) str;

    if (!str || addr >= UACCESS_LIMIT)
        return -EFAULT;

    for (; addr < UACCESS_LIMIT; ++addr) {
        if (!*(const char *) addr)
            return 0;
    }

    return -EFAULT;
}

#endif /* ! _SYS_UACCESS_H */
