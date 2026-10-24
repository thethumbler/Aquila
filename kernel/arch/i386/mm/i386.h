#ifndef _PAGING_I386_H
#define _PAGING_I386_H

#include <core/system.h>

/* i386: the bits of a page table entry, and an address's indexes in the
 * page directory and in a page table. */

#define PG_PRESENT  1
#define PG_WRITE    2
#define PG_USER     4

#define VTBL(n) (((n) >> 12) & 0x3ff)
#define VDIR(n) (((n) >> 22) & 0x3ff)

#define PHYSADDR(s) ((s) & ~0xfff)

#endif /* ! _PAGING_I386_H */
