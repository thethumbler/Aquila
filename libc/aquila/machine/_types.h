#ifndef _MACHINE__TYPES_H
#define _MACHINE__TYPES_H

#include <machine/_default_types.h>

/* the kernel uses 32-bit user and group ids (struct aquila_stat): an int
 * on LP64 (x86-64, aarch64), a long on i386 */
#if __SIZEOF_LONG__ == 8
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

/* the kernel's inode numbers are 64-bit (struct aquila_stat), some are
 * pointers: newlib's default, an unsigned short, cut them */
#define __machine_ino_t_defined
typedef __uint64_t __ino_t;

#endif /* ! _MACHINE__TYPES_H */
