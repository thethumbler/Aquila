#ifndef _CPU_IO_H
#define _CPU_IO_H

#include <stdint.h>

#define IOADDR_PORT    1
#define IOADDR_MMIO8   2
#define IOADDR_MMIO16  3
#define IOADDR_MMIO32  4

struct ioaddr {
    char type;
    uintptr_t addr;
};

static inline char *ioaddr_type_str(struct ioaddr *io)
{
    switch (io->type) {
        case IOADDR_PORT:
            return "pio";
        case IOADDR_MMIO8:
            return "mmio8";
        case IOADDR_MMIO16:
            return "mmio16";
        case IOADDR_MMIO32:
            return "mmio32";
    }

    return NULL;
}

#endif /* ! _CPU_IO_H */
