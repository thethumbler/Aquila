#ifndef _NONE_CPU_IO_H
#define _NONE_CPU_IO_H

#include_next <cpu/io.h>

static inline uint8_t io_in8(struct ioaddr *io, uintptr_t off)
{
    return 0;
}

static inline uint16_t io_in16(struct ioaddr *io, uintptr_t off)
{
    return 0;
}

static inline uint32_t io_in32(struct ioaddr *io, uintptr_t off)
{
    return 0;
}

static inline void io_out8(struct ioaddr *io, uintptr_t off, uint8_t val)
{
}

static inline void io_out16(struct ioaddr *io, uintptr_t off, uint16_t val)
{
}

static inline void io_out32(struct ioaddr *io, uintptr_t off, uint32_t val)
{
}

#endif /* ! _NONE_CPU_IO_H */
