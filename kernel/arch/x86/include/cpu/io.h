#ifndef _X86_IO_H
#define _X86_IO_H

#include <stdint.h>

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

#define __insw(port, count, buf) \
({ \
    asm volatile ("rep insw"::"D"(buf), "c"(count), "d"(port):"memory"); \
})

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

#define __io_wait() \
({ \
    asm volatile ( "jmp 1f\n\t" \
                   "1:jmp 2f\n\t" \
                   "2:" ); \
})

#define __mmio_inb(addr) (*((volatile uint8_t *)(addr)))
#define __mmio_inw(addr) (*((volatile uint16_t *)(addr)))
#define __mmio_inl(addr) (*((volatile uint32_t *)(addr)))
#define __mmio_inq(addr) (*((volatile uint64_t *)(addr)))

#define __mmio_outb(addr, v) (*((volatile uint8_t  *)(addr)) = v)
#define __mmio_outw(addr, v) (*((volatile uint16_t *)(addr)) = v)
#define __mmio_outl(addr, v) (*((volatile uint32_t *)(addr)) = v)
#define __mmio_outq(addr, v) (*((volatile uint64_t *)(addr)) = v)

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

#endif /* !_X86_IO_H */
