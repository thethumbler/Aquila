/** \file
 * ATA programmed I/O (PIO): the CPU moves the data itself.
 *
 * For each command, the driver writes the sectors' address, sends the
 * command, then waits for the drive to be ready for each sector by
 * polling its status, and copies the sector through the data port, 256
 * words at a time. It works with any drive, but keeps the CPU busy; bus
 * master DMA (`dma.c`) is used instead when the controller and the drive
 * can. Only LBA addressing is supported, not CHS.
 */

#include <core/system.h>
#include <core/panic.h>
#include <cpu/io.h>
#include <dev/dev.h>
#include <fs/vfs.h>
#include <fs/devfs.h>
#include <fs/posix.h>
#include <fs/mbr.h>
#include <aquila/errno.h>
#include <dev/pci.h>

#include <ata.h>

#define PIO_MAX_RETRIES 5

/* the most sectors per command: 256 can be given both with LBA28 (as 0)
 * and LBA48 */
#define PIO_MAX_SECTORS 256

/** Reads up to `PIO_MAX_SECTORS` sectors with one command, trying it
 * again a few times if the drive aborts it. Returns 0 or a negative
 * errno. */
static ssize_t pio_read_sectors(struct ata_drive *drive, uint64_t lba, size_t count, void *buf)
{
    int retry_count = 0;

retry:
    if (++retry_count == PIO_MAX_RETRIES)
        return -EIO;

    if (drive->capabilities & ATA_CAP_LBA) {
        /* the sectors' address and count */
        ata_setup(drive, lba, count);

        /* the read command */
        ata_wait(drive);

        if (drive->mode == ATA_MODE_LBA48)
            io_out8(&drive->base, ATA_REG_CMD, ATA_CMD_READ_SECTORS_EXT);
        else
            io_out8(&drive->base, ATA_REG_CMD, ATA_CMD_READ_SECTORS);

        ata_wait(drive);

        size_t _count = count;
        char *_buf = buf;

        while (_count--) {
            int err;
            if ((err = ata_poll(drive, 1))) {

                /* the drive aborted the command: try it again, a few
                 * times */
                if (err == -EAGAIN) {
                    printk("ata: retrying...\n");

                    for (int i = 0; i < 5; ++i)
                        ata_wait(drive);
                    goto retry;
                }

                return -EIO;
            }

            /* copy a whole sector with one instruction: under KVM that is
             * one exit to the hypervisor, rather than one per word */
            __insw(drive->base.addr + ATA_REG_DATA, 256, _buf);
            _buf += 512;
        }

        if (ata_poll(drive, 0))
            return -EIO;
    } else {
        /* CHS addressing isn't supported */
        return -ENOTSUP;
    }

    return 0;
}

/** Writes up to `PIO_MAX_SECTORS` sectors with one command. Returns 0 or a
 * negative errno. */
static ssize_t pio_write_sectors(struct ata_drive *drive, uint64_t lba, size_t count, void *buf)
{
    if (!(drive->capabilities & ATA_CAP_LBA))
        return -ENOTSUP;    /* CHS addressing isn't supported */

    ata_setup(drive, lba, count);

    /* the write command */
    ata_wait(drive);

    if (drive->mode == ATA_MODE_LBA48)
        io_out8(&drive->base, ATA_REG_CMD, ATA_CMD_WRITE_SECTORS_EXT);
    else
        io_out8(&drive->base, ATA_REG_CMD, ATA_CMD_WRITE_SECTORS);

    while (count--) {
        uint16_t *_buf = (uint16_t *) buf;

        if (ata_poll(drive, 1)) return -EIO;

        __outsw(drive->base.addr + ATA_REG_DATA, 256, _buf);
        buf += 512;
    }

    /* the drive may keep the data in its write cache, until pio_flush() */
    if (ata_poll(drive, 0)) return -EIO;

    return 0;
}

/** Has the drive write what its cache holds to the disk. Returns 0, or
 * `-EIO` if it failed. */
static int pio_flush(struct ata_drive *drive)
{
    ata_select_drive(drive, drive->mode);
    ata_wait(drive);

    if (drive->mode == ATA_MODE_LBA48)
        io_out8(&drive->base, ATA_REG_CMD, ATA_CMD_CACHE_FLUSH_EXT);
    else
        io_out8(&drive->base, ATA_REG_CMD, ATA_CMD_CACHE_FLUSH);

    return ata_poll(drive, 0) ? -EIO : 0;
}

/** Reads `count` sectors from `lba` on, `PIO_MAX_SECTORS` per command.
 * Returns 0 or a negative errno. */
static ssize_t pio_read(struct ata_drive *drive, uint64_t lba, size_t count, void *buf)
{
    while (count) {
        size_t n = MIN(count, PIO_MAX_SECTORS);
        ssize_t err;

        if ((err = pio_read_sectors(drive, lba, n, buf)) < 0)
            return err;

        lba   += n;
        count -= n;
        buf   += n * 512;
    }

    return 0;
}

/** Writes `count` sectors from `lba` on, `PIO_MAX_SECTORS` per command.
 * Returns 0 or a negative errno. */
static ssize_t pio_write(struct ata_drive *drive, uint64_t lba, size_t count, void *buf)
{
    while (count) {
        size_t n = MIN(count, PIO_MAX_SECTORS);
        ssize_t err;

        if ((err = pio_write_sectors(drive, lba, n, buf)) < 0)
            return err;

        lba   += n;
        count -= n;
        buf   += n * 512;
    }

    return 0;
}

/**
 * Sets a drive up for PIO, and resets its channel.
 *
 * @param drive  the drive
 */
void pio_init(struct ata_drive *drive)
{
    drive->read  = pio_read;
    drive->write = pio_write;
    drive->flush = pio_flush;
    ata_reset(drive);
}
