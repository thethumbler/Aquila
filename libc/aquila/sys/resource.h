#ifndef _SYS_RESOURCE_H_
#define _SYS_RESOURCE_H_

#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RUSAGE_SELF     0   /* calling process */
#define RUSAGE_CHILDREN -1  /* terminated child processes */

struct rusage {
    struct timeval ru_utime;    /* user time used */
    struct timeval ru_stime;    /* system time used */
};

int getrusage(int who, struct rusage *usage);

/* resource limits: the kernel has none but the number of descriptors */
typedef unsigned long rlim_t;

#define RLIM_INFINITY   ((rlim_t) -1)
#define RLIM_SAVED_MAX  RLIM_INFINITY
#define RLIM_SAVED_CUR  RLIM_INFINITY

#define RLIMIT_CPU      0
#define RLIMIT_FSIZE    1
#define RLIMIT_DATA     2
#define RLIMIT_STACK    3
#define RLIMIT_CORE     4
#define RLIMIT_RSS      5
#define RLIMIT_NPROC    6
#define RLIMIT_NOFILE   7
#define RLIMIT_MEMLOCK  8
#define RLIMIT_AS       9
#define RLIM_NLIMITS    10

struct rlimit {
    rlim_t rlim_cur;    /* soft limit */
    rlim_t rlim_max;    /* hard limit */
};

int getrlimit(int resource, struct rlimit *rlp);
int setrlimit(int resource, const struct rlimit *rlp);

#ifdef __cplusplus
}
#endif

#endif /* !_SYS_RESOURCE_H_ */
