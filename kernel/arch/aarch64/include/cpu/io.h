#ifndef _AARCH64_IO_H
#define _AARCH64_IO_H

#include_next <cpu/io.h>

/*
 * Devices' registers: memory-mapped only (aarch64 has no I/O ports), at
 * io->addr (a kernel address: arch_mmio_map()'s) plus `off' registers,
 * a byte apart (IOADDR_MMIO8), two (MMIO16) or four (MMIO32)
 */

static inline uintptr_t io_reg(struct ioaddr *io, uintptr_t off)
{
    switch (io->type) {
        case IOADDR_MMIO16:
            return io->addr + (off << 1);
        case IOADDR_MMIO32:
            return io->addr + (off << 2);
    }

    return io->addr + off;
}

static inline uint8_t io_in8(struct ioaddr *io, uintptr_t off)
{
    return *(volatile uint8_t *) io_reg(io, off);
}

static inline uint16_t io_in16(struct ioaddr *io, uintptr_t off)
{
    return *(volatile uint16_t *) io_reg(io, off);
}

static inline uint32_t io_in32(struct ioaddr *io, uintptr_t off)
{
    return *(volatile uint32_t *) io_reg(io, off);
}

static inline void io_out8(struct ioaddr *io, uintptr_t off, uint8_t val)
{
    *(volatile uint8_t *) io_reg(io, off) = val;
}

static inline void io_out16(struct ioaddr *io, uintptr_t off, uint16_t val)
{
    *(volatile uint16_t *) io_reg(io, off) = val;
}

static inline void io_out32(struct ioaddr *io, uintptr_t off, uint32_t val)
{
    *(volatile uint32_t *) io_reg(io, off) = val;
}

/* a 32-bit register at a kernel address */
static inline uint32_t mmio_read32(uintptr_t addr)
{
    return *(volatile uint32_t *) addr;
}

static inline void mmio_write32(uintptr_t addr, uint32_t val)
{
    *(volatile uint32_t *) addr = val;
}

#endif /* ! _AARCH64_IO_H */
