#ifndef _TYPES_H
#define _TYPES_H

#include <stdint.h>

typedef int       pid_t;
typedef pid_t     tid_t;
typedef long int  off_t;

#if !defined(__TINYC__)
typedef long int  ssize_t;
#endif

typedef uintptr_t vino_t;    /* vino_t should be large enough to hold a pointer */
typedef uint32_t  mode_t;
typedef uint8_t   mask_t;
typedef uint8_t   devid_t;
typedef uint16_t  dev_t;

typedef uint32_t  mode_t;
typedef uint32_t  uid_t;
typedef uint32_t  gid_t;
typedef uint32_t  nlink_t;
typedef uintptr_t ino_t;
typedef struct timespec _time_t;

typedef uint64_t time_t;
typedef unsigned long sigset_t;

struct timespec {
    time_t   tv_sec;
    uint32_t tv_nsec;
};

#endif
