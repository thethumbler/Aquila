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

/* max sectors per command, 256 is encodable in both LBA28 (as 0) and LBA48 */
#define PIO_MAX_SECTORS 256

static ssize_t pio_read_sectors(struct ata_drive *drive, uint64_t lba, size_t count, void *buf)
{
    //printk("pio_read(drive=%p, lba=%ld, count=%d, buf=%p)\n", drive, lba, count, buf);

    int retry_count = 0;

retry:
    if (++retry_count == PIO_MAX_RETRIES)
        return -EIO;

    if (drive->capabilities & ATA_CAP_LBA) {
        /* Use LBA mode */
        ata_setup(drive, lba, count);

        /* Send read command */
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

                /* retry */
                if (err == -ATA_ERROR_ABRT) {
                    printk("ata: retrying...\n");

                    for (int i = 0; i < 5; ++i)
                        ata_wait(drive);
                    goto retry;
                }

                return -EIO;
            }

            /* a sector at once, one exit to the hypervisor under KVM
             * rather than one per word */
            __insw(drive->base.addr + ATA_REG_DATA, 256, _buf);
            _buf += 512;
        }

        if (ata_poll(drive, 0))
            return -EINVAL;
    } else {
        /* TODO CHS */
        return -ENOTSUP;
    }

    return 0;
}

static ssize_t pio_write_sectors(struct ata_drive *drive, uint64_t lba, size_t count, void *buf)
{
    if (!(drive->capabilities & ATA_CAP_LBA))
        return -ENOTSUP;    /* TODO CHS */

    ata_setup(drive, lba, count);

    /* Send write command */
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

    /* the drive may keep it in its write cache, until pio_flush() */
    if (ata_poll(drive, 0)) return -EIO;

    return 0;
}

/* writes in the drive's cache to the disk */
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

void pio_init(struct ata_drive *drive)
{
    drive->read  = pio_read;
    drive->write = pio_write;
    drive->flush = pio_flush;
    ata_reset(drive);
}
