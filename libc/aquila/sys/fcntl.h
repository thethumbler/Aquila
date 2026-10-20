#ifndef _SYS_FCNTL_H_
#define _SYS_FCNTL_H_

#include <_ansi.h>
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/stat.h>

/* O_*, F_* and AT_* values, shared with the kernel */
#include <aquila/fcntl.h>

#ifdef __cplusplus
extern "C" {
#endif

#define F_RDLCK     1   /* read lock */
#define F_WRLCK     2   /* write lock */
#define F_UNLCK     3   /* remove lock(s) */

#if __BSD_VISIBLE
#define LOCK_SH     0x01    /* shared file lock */
#define LOCK_EX     0x02    /* exclusive file lock */
#define LOCK_NB     0x04    /* don't block when locking */
#define LOCK_UN     0x08    /* unlock file */
#endif

/* file record locking, not supported by the kernel yet */
struct flock {
    short   l_type;     /* F_RDLCK, F_WRLCK, or F_UNLCK */
    short   l_whence;   /* flag to choose starting offset */
    long    l_start;    /* relative offset, in bytes */
    long    l_len;      /* length, in bytes; 0 means lock to EOF */
    short   l_pid;      /* returned with F_GETLK */
    short   l_xxx;      /* reserved for future use */
};

extern int open(const char *, int, ...);
#if __ATFILE_VISIBLE
extern int openat(int, const char *, int, ...);
#endif
extern int creat(const char *, mode_t);
extern int fcntl(int, int, ...);
#if __BSD_VISIBLE
extern int flock(int, int);
#endif

#ifdef __cplusplus
}
#endif

#endif /* !_SYS_FCNTL_H_ */
