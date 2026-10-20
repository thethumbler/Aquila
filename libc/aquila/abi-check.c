/*
 * Compile-time check that the C library agrees with the kernel ABI
 * (include/uapi), run by toolchain/build.sh with -Werror after libc is
 * installed. Nothing here ends up in libc.
 *
 * errno and signal numbers: newlib's headers are included first, a
 * different value in <aquila/...> is then a macro redefinition error.
 */

#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <time.h>

#include <aquila/errno.h>
#include <aquila/signal.h>
#include <aquila/stat.h>
#include <aquila/time.h>

#define SAME_FIELD(a, b, f) \
    _Static_assert(offsetof(struct a, f) == offsetof(struct b, f) && \
                   sizeof(((struct a *) 0)->f) == sizeof(((struct b *) 0)->f), \
                   "struct " #a " ." #f " does not match the kernel")

#define SAME_SIZE(a, b) \
    _Static_assert(sizeof(struct a) == sizeof(struct b), \
                   "struct " #a " size does not match the kernel")

SAME_SIZE(stat, aquila_stat);
SAME_FIELD(stat, aquila_stat, st_dev);
SAME_FIELD(stat, aquila_stat, st_ino);
SAME_FIELD(stat, aquila_stat, st_mode);
SAME_FIELD(stat, aquila_stat, st_nlink);
SAME_FIELD(stat, aquila_stat, st_uid);
SAME_FIELD(stat, aquila_stat, st_gid);
SAME_FIELD(stat, aquila_stat, st_rdev);
SAME_FIELD(stat, aquila_stat, st_size);
SAME_FIELD(stat, aquila_stat, st_atim);
SAME_FIELD(stat, aquila_stat, st_mtim);
SAME_FIELD(stat, aquila_stat, st_ctim);
SAME_FIELD(stat, aquila_stat, st_blksize);
SAME_FIELD(stat, aquila_stat, st_blocks);

SAME_SIZE(sigaction, aquila_sigaction);
SAME_FIELD(sigaction, aquila_sigaction, sa_handler);
SAME_FIELD(sigaction, aquila_sigaction, sa_mask);
SAME_FIELD(sigaction, aquila_sigaction, sa_flags);

/* the kernel's struct timeval (gettimeofday) is 64-bit seconds, long usec */
_Static_assert(sizeof(struct timeval) == 12 &&
               offsetof(struct timeval, tv_usec) == 8,
               "struct timeval does not match the kernel");

/* utimensat(2), nanosleep(2) and clock_gettime(2) take a struct aquila_timespec */
SAME_SIZE(timespec, aquila_timespec);
SAME_FIELD(timespec, aquila_timespec, tv_sec);
SAME_FIELD(timespec, aquila_timespec, tv_nsec);

/* defined by the kernel in <aquila/stat.h> and <aquila/time.h>, which skip
 * them when the C library's headers came first */
_Static_assert(UTIME_NOW == -2 && UTIME_OMIT == -1, "UTIME_NOW/UTIME_OMIT do not match the kernel");
_Static_assert(CLOCK_REALTIME == 1 && CLOCK_MONOTONIC == 4, "clock ids do not match the kernel");

/* times(2) */
SAME_SIZE(tms, aquila_tms);
SAME_FIELD(tms, aquila_tms, tms_utime);
SAME_FIELD(tms, aquila_tms, tms_cstime);
