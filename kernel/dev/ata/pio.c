#include <core/system.h>
#include <core/panic.h>
#include <cpu/io.h>
#include <dev/dev.h>
#include <fs/vfs.h>
#include <fs/devfs.h>
#include <fs/posix.h>
#include <fs/mbr.h>
#include <bits/errno.h>
#include <dev/pci.h>

#include <ata.h>

#define PIO_MAX_RETRIES 5

static ssize_t pio_read(struct ata_drive *drive, uint64_t lba, size_t count, void *buf)
{
    //printk("pio_read(drive=%p, lba=%ld, count=%d, buf=%p)\n", drive, lba, count, buf);

    int retry_count = 0;

retry:
    if (++retry_count == PIO_MAX_RETRIES)
        return -EIO;

    if (drive->capabilities & ATA_CAP_LBA) {
        /* Use LBA mode */
        ata_select_drive(drive, drive->mode);

        /* Send NULL byte to error register */
        io_out8(&drive->base, ATA_REG_ERROR, 0);

        /* Send sectors count and LBA */
        if (drive->mode == ATA_MODE_LBA48) {
            io_out8(&drive->base, ATA_REG_SECCOUNT0, (count >> 8) & 0xFF);
            io_out8(&drive->base, ATA_REG_LBA0, (uint8_t) (lba >> (8 * 3)));
            io_out8(&drive->base, ATA_REG_LBA1, (uint8_t) (lba >> (8 * 4)));
            io_out8(&drive->base, ATA_REG_LBA2, (uint8_t) (lba >> (8 * 5)));
        }

        io_out8(&drive->base, ATA_REG_SECCOUNT0, count & 0xFF);
        io_out8(&drive->base, ATA_REG_LBA0, (uint8_t) (lba >> (8 * 0)));
        io_out8(&drive->base, ATA_REG_LBA1, (uint8_t) (lba >> (8 * 1)));
        io_out8(&drive->base, ATA_REG_LBA2, (uint8_t) (lba >> (8 * 2)));

        /* Send read command */
        ata_wait(drive);

        if (drive->mode == ATA_MODE_LBA48)
            io_out8(&drive->base, ATA_REG_CMD, ATA_CMD_READ_SECTORS_EXT);
        else
            io_out8(&drive->base, ATA_REG_CMD, ATA_CMD_READ_SECTORS);

        ata_wait(drive);

        size_t _count = count;

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

            /* FIXME */
            //__insw(drive->base.addr + ATA_REG_DATA, 256, buf);
            char *_buf = buf;

            for (int i = 0; i < 256; ++i) {
                uint16_t x = io_in16(&drive->base, ATA_REG_DATA);
                _buf[2*i+0] = x & 0xFFFF;
                _buf[2*i+1] = (x >> 8) & 0xFFFF;
            }

            buf += 512;
        }

        if (ata_poll(drive, 0))
            return -EINVAL;
    } else {
        /* TODO CHS */
    }

    return 0;
}

static ssize_t pio_write(struct ata_drive *drive, uint64_t lba, size_t count, void *buf)
{
    ata_select_drive(drive, ATA_MODE_LBA48);

    /* Send NULL byte to error port */
    io_out8(&drive->base, ATA_REG_ERROR, 0);

    /* Send sectors count and LBA */
    io_out8(&drive->base, ATA_REG_SECCOUNT0, (count >> 8) & 0xFF);
    io_out8(&drive->base, ATA_REG_LBA0, (uint8_t) (lba >> (8 * 3)));
    io_out8(&drive->base, ATA_REG_LBA1, (uint8_t) (lba >> (8 * 4)));
    io_out8(&drive->base, ATA_REG_LBA2, (uint8_t) (lba >> (8 * 5)));
    io_out8(&drive->base, ATA_REG_SECCOUNT0, count & 0xFF);
    io_out8(&drive->base, ATA_REG_LBA0, (uint8_t) (lba >> (8 * 0)));
    io_out8(&drive->base, ATA_REG_LBA1, (uint8_t) (lba >> (8 * 1)));
    io_out8(&drive->base, ATA_REG_LBA2, (uint8_t) (lba >> (8 * 2)));

    /* Send write command */
    ata_wait(drive);
    io_out8(&drive->base, ATA_REG_CMD, ATA_CMD_WRITE_SECTORS_EXT);

    while (count--) {
        uint16_t *_buf = (uint16_t *) buf;

        if (ata_poll(drive, 1)) return -EINVAL;

        for (int i = 0; i < 256; ++i)
            io_out16(&drive->base, ATA_REG_DATA, _buf[i]);

        buf += 512;
    }

    if (ata_poll(drive, 0)) return -EINVAL;
    io_out8(&drive->base, ATA_REG_CMD, ATA_CMD_CACHE_FLUSH);
    if (ata_poll(drive, 0)) return -EINVAL;

    return 0;
}

void pio_init(struct ata_drive *drive)
{
    drive->read  = pio_read;
    drive->write = pio_write;
    ata_soft_reset(drive);
}
