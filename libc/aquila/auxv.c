/*
 * getauxval(): a value from the auxiliary vector, which the kernel puts
 * on a new program's initial stack and crt0 finds (__auxv). As on Linux,
 * a type that isn't in the vector gives 0, with errno set to ENOENT.
 */

#include <errno.h>
#include <sys/auxv.h>

extern unsigned long *__auxv;

unsigned long getauxval(unsigned long type)
{
    for (unsigned long *a = __auxv; a && a[0] != AT_NULL; a += 2)
        if (a[0] == type)
            return a[1];

    errno = ENOENT;
    return 0;
}
