#ifndef	_SYS_FCNTL_H_
#ifdef __cplusplus
extern "C" {
#endif
#define	_SYS_FCNTL_H_
#include <_ansi.h>
#include <sys/cdefs.h>

#define	O_ACCMODE	(O_RDONLY|O_WRONLY|O_RDWR)

#define	O_RDONLY	0x000000
#define	O_WRONLY	0x000001
#define	O_RDWR		0x000002
#define	O_APPEND	0x000008
#define	O_CREAT		0x000200
#define	O_TRUNC		0x000400
#define	O_EXCL		0x000800
#define O_SYNC		0x002000
#define O_DSYNC     O_SYNC
#define O_RSYNC     O_SYNC
#define	O_NONBLOCK	0x004000
#define	O_NOCTTY	0x008000
#define O_CLOEXEC	0x040000
#define O_NOFOLLOW  0x100000
#define O_DIRECTORY 0x200000
#define O_EXEC      0x400000 
#define O_SEARCH    O_EXEC

/* fcntl(2) requests */
#define F_DUPFD         0   /* Duplicate fildes */
#define F_GETFD         1   /* Get fildes flags (close on exec) */
#define F_SETFD         2   /* Set fildes flags (close on exec) */
#define F_GETFL         3   /* Get file flags */
#define F_SETFL         4   /* Set file flags */
#define F_GETOWN        5   /* Get owner - for ASYNC */
#define F_SETOWN        6   /* Set owner - for ASYNC */
#define F_GETLK         7   /* Get record-locking information */
#define F_SETLK         8   /* Set or Clear a record-lock (Non-Blocking) */
#define F_SETLKW        9   /* Set or Clear a record-lock (Blocking) */
#define F_RGETLK        10  /* Test a remote lock to see if it is blocked */
#define F_RSETLK        11  /* Set or unlock a remote lock */
#define F_CNVT          12  /* Convert a fhandle to an open fd */
#define F_RSETLKW       13  /* Set or Clear remote record-lock(Blocking) */
#define F_DUPFD_CLOEXEC 14  /* As F_DUPFD, but set close-on-exec flag */

#define FD_CLOEXEC      1

#include <sys/types.h>
#include <sys/stat.h>		/* sigh. for the mode bits for open/creat */

extern int open (const char *, int, ...);
#if __ATFILE_VISIBLE
extern int openat (int, const char *, int, ...);
#endif
extern int creat (const char *, mode_t);
extern int fcntl (int, int, ...);
#if __BSD_VISIBLE
extern int flock (int, int);
#endif

#ifdef __cplusplus
}
#endif
#endif	/* !_SYS_FCNTL_H_ */
