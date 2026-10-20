#ifndef _MACHINE__TYPES_H
#define _MACHINE__TYPES_H

#include <machine/_default_types.h>

/* the kernel uses 32-bit user and group ids (struct aquila_stat) */
#define __machine_uid_t_defined
typedef unsigned long __uid_t;

#define __machine_gid_t_defined
typedef unsigned long __gid_t;

#endif /* ! _MACHINE__TYPES_H */
