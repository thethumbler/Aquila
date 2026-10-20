/*
 * stdio stream locking, newlib leaves these to the system port.
 *
 * newlib's lock primitives are no-ops on this target (sys/lock.h), so
 * stream locks are not held across threads yet.
 */

#include <stdio.h>
#include <sys/stdio.h>

void flockfile(FILE *fp)
{
    _flockfile(fp);
}

int ftrylockfile(FILE *fp)
{
    /* the lock can always be taken */
    return 0;
}

void funlockfile(FILE *fp)
{
    _funlockfile(fp);
}
