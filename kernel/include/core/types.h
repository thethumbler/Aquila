#ifndef _TYPES_H
#define _TYPES_H

#include <stdint.h>

typedef int       pid_t;
typedef long int  off_t;
typedef long int  ssize_t;
typedef uintptr_t vino_t;    /* vino_t should be large enough to hold a pointer */
typedef uint32_t  mode_t;
typedef uint8_t   mask_t;
typedef uint8_t   itype_t;
typedef uint8_t   devid_t;
typedef uint16_t  dev_t;

#endif
