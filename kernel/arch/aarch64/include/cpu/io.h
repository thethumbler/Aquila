#ifndef _AARCH64_IO_H
#define _AARCH64_IO_H

#include_next <cpu/io.h>

/*
 * Reading and writing devices' registers. On aarch64 they are only ever
 * memory-mapped (there are no I/O ports): register `off` is at
 * `io->addr` (a kernel address, from arch_mmio_map()) plus `off`
 * registers, which are a byte apart (IOADDR_MMIO8), two bytes
 * (IOADDR_MMIO16), or four (IOADDR_MMIO32).
 */

/** Returns the kernel address of register `off`. */
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

/** Reads an 8-, 16- or 32-bit register. */
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

/** Writes an 8-, 16- or 32-bit register. */
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

/** Reads, or writes, a 32-bit register at a kernel address. */
static inline uint32_t mmio_read32(uintptr_t addr)
{
    return *(volatile uint32_t *) addr;
}

static inline void mmio_write32(uintptr_t addr, uint32_t val)
{
    *(volatile uint32_t *) addr = val;
}

#endif /* ! _AARCH64_IO_H */
