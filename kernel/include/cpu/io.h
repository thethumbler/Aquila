#ifndef _CPU_IO_H
#define _CPU_IO_H

#include <stdint.h>

/*
 * A device's registers, wherever they are: I/O ports (on x86), or memory.
 * Each architecture's cpu/io.h adds io_in8() and the others, which read
 * and write them. A memory-mapped device's registers may be a byte, two
 * or four bytes apart (IOADDR_MMIO8, MMIO16, MMIO32).
 */

/* What kind of registers an ioaddr has. */
#define IOADDR_PORT    1
#define IOADDR_MMIO8   2
#define IOADDR_MMIO16  3
#define IOADDR_MMIO32  4

/** Where a device's registers are. */
struct ioaddr {
    char type;          /* IOADDR_* */
    uintptr_t addr;     /* the first port, or the kernel address of the first register */
};

/** Returns the name of an ioaddr's kind, for messages. */
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
