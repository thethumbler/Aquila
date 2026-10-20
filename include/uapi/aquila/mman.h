#ifndef _AQUILA_MMAN_H
#define _AQUILA_MMAN_H

#define PROT_NONE   0x00000
#define PROT_READ   0x00001
#define PROT_WRITE  0x00002
#define PROT_EXEC   0x00004

#define MAP_FAILED  ((void *) -1)
#define MAP_FIXED   0x00001
#define MAP_PRIVATE 0x00002
#define MAP_SHARED  0x00004
#define MAP_ANONYMOUS 0x00008   /* zero-filled memory, no file (private) */
#define MAP_ANON    MAP_ANONYMOUS

#endif /* ! _AQUILA_MMAN_H */
