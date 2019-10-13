#ifndef _I386_MM_PMAP_H
#define _I386_MM_PMAP_H

struct pmap {
    paddr_t map;
    size_t  ref;
};

#include_next <mm/pmap.h>

#endif /* ! _I386_MM_PMAP_H */
