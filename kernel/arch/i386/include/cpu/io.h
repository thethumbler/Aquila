#ifndef _X86_IO_H
#define _X86_IO_H

#include <stdint.h>

/*
 * x86: devices' registers, as I/O ports or memory. Drivers use the
 * io_in*() and io_out*() functions below with a struct ioaddr, which says
 * which of the two a device's registers are, and how far apart they are.
 * The __in* and __out* macros are the port instructions themselves.
 */

/* read a port */
#define __inb(port) \
({ \
    uint8_t ret; \
    asm volatile ("inb %%dx, %%al":"=a"(ret):"d"(port)); \
    ret; \
})

#define __inw(port) \
({ \
    uint16_t ret; \
    asm volatile ("inw %%dx, %%ax":"=a"(ret):"dN"(port)); \
    ret; \
})

#define __inl(port) \
({ \
    uint32_t ret; \
    asm volatile ("inl %%dx, %%eax":"=a"(ret):"d"(port)); \
    ret; \
})

/* Read or write `count` 16-bit words through a port. rep insw and rep
 * outsw change EDI (ESI) and ECX, so those are outputs for the
 * compiler. */
#define __insw(port, count, buf) \
({ \
    void *__buf = (buf); \
    uint32_t __count = (count); \
    asm volatile ("rep insw":"+D"(__buf), "+c"(__count):"d"(port):"memory"); \
})

#define __outsw(port, count, buf) \
({ \
    const void *__buf = (buf); \
    uint32_t __count = (count); \
    asm volatile ("rep outsw":"+S"(__buf), "+c"(__count):"d"(port):"memory"); \
})

/* write a port */
#define __outb(port, value) \
({ \
    asm volatile ("outb %%al, %%dx"::"d"((port)),"a"((value))); \
})

#define __outw(port, value) \
({ \
    asm volatile ("outw %%ax, %%dx"::"d"(port),"a"(value)); \
})

#define __outl(port, value) \
({ \
    asm volatile ("outl %%eax, %%dx"::"d"(port),"a"(value)); \
})

/* a short delay, for old devices that need time between accesses */
#define __io_wait() \
({ \
    asm volatile ( "jmp 1f\n\t" \
                   "1:jmp 2f\n\t" \
                   "2:" ); \
})

/* read or write a memory-mapped register */
#define __mmio_inb(addr) (*((volatile uint8_t *)(addr)))
#define __mmio_inw(addr) (*((volatile uint16_t *)(addr)))
#define __mmio_inl(addr) (*((volatile uint32_t *)(addr)))
#define __mmio_inq(addr) (*((volatile uint64_t *)(addr)))

#define __mmio_outb(addr, v) (*((volatile uint8_t  *)(addr)) = v)
#define __mmio_outw(addr, v) (*((volatile uint16_t *)(addr)) = v)
#define __mmio_outl(addr, v) (*((volatile uint32_t *)(addr)) = v)
#define __mmio_outq(addr, v) (*((volatile uint64_t *)(addr)) = v)

/* What a device's registers are: I/O ports, or memory with registers 1,
 * 2 or 4 bytes apart. */
#define IOADDR_PORT    1
#define IOADDR_MMIO8   2
#define IOADDR_MMIO16  3
#define IOADDR_MMIO32  4

/** Where a device's registers are. */
struct ioaddr {
    char type;          /* IOADDR_* */
    uintptr_t addr;     /* the first port, or the kernel address of the first register */
};

/* Returns the name of an ioaddr's type, for messages. */
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

/* Read or write register number `off` of a device, 8, 16 or 32 bits at
 * a time. */
static inline uint8_t io_in8(struct ioaddr *io, uintptr_t off)
{
    switch (io->type) {
        case IOADDR_PORT:
            return __inb(io->addr + off);
        case IOADDR_MMIO8:
            return __mmio_inb(io->addr + off);
        case IOADDR_MMIO16:
            return __mmio_inb(io->addr + (off << 1));
        case IOADDR_MMIO32:
            return __mmio_inb(io->addr + (off << 2));
    }

    return 0;
}

static inline uint16_t io_in16(struct ioaddr *io, uintptr_t off)
{
    switch (io->type) {
        case IOADDR_PORT:
            return __inw(io->addr + off);
        case IOADDR_MMIO8:
            return __mmio_inw(io->addr + off);
        case IOADDR_MMIO16:
            return __mmio_inw(io->addr + (off << 1));
        case IOADDR_MMIO32:
            return __mmio_inw(io->addr + (off << 2));
    }

    return 0;
}

static inline uint32_t io_in32(struct ioaddr *io, uintptr_t off)
{
    switch (io->type) {
        case IOADDR_PORT:
            return __inl(io->addr + off);
        case IOADDR_MMIO8:
            return __mmio_inl(io->addr + off);
        case IOADDR_MMIO16:
            return __mmio_inl(io->addr + (off << 1));
        case IOADDR_MMIO32:
            return __mmio_inl(io->addr + (off << 2));
    }

    return 0;
}

static inline void io_out8(struct ioaddr *io, uintptr_t off, uint8_t val)
{
    switch (io->type) {
        case IOADDR_PORT:
            __outb(io->addr + off, val); return;
        case IOADDR_MMIO8:
            __mmio_outb(io->addr + off, val); return;
        case IOADDR_MMIO16:
            __mmio_outb(io->addr + (off << 1), val); return;
        case IOADDR_MMIO32:
            __mmio_outb(io->addr + (off << 2), val); return;
    }
}

static inline void io_out16(struct ioaddr *io, uintptr_t off, uint16_t val)
{
    switch (io->type) {
        case IOADDR_PORT:
            __outw(io->addr + off, val); return;
        case IOADDR_MMIO8:
            __mmio_outw(io->addr + off, val); return;
        case IOADDR_MMIO16:
            __mmio_outw(io->addr + (off << 1), val); return;
        case IOADDR_MMIO32:
            __mmio_outw(io->addr + (off << 2), val); return;
    }
}

static inline void io_out32(struct ioaddr *io, uintptr_t off, uint32_t val)
{
    switch (io->type) {
        case IOADDR_PORT:
            __outl(io->addr + off, val); return;
        case IOADDR_MMIO8:
            __mmio_outl(io->addr + off, val); return;
        case IOADDR_MMIO16:
            __mmio_outl(io->addr + (off << 1), val); return;
        case IOADDR_MMIO32:
            __mmio_outl(io->addr + (off << 2), val); return;
    }
}

#endif /* ! _X86_IO_H */
