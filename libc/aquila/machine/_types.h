#ifndef _MACHINE__TYPES_H
#define _MACHINE__TYPES_H

#include <machine/_default_types.h>

/* the kernel uses 32-bit user and group ids (struct aquila_stat) */
#ifdef __x86_64__
#define __machine_uid_t_defined
typedef unsigned int __uid_t;

#define __machine_gid_t_defined
typedef unsigned int __gid_t;
#else
#define __machine_uid_t_defined
typedef unsigned long __uid_t;

#define __machine_gid_t_defined
typedef unsigned long __gid_t;
#endif

#endif /* ! _MACHINE__TYPES_H */
