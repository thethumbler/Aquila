/** \file
 * The ATA driver: hard disks on the IDE controller, as block device 3
 * (`/dev/hda` and up).
 *
 * The driver finds the IDE controller on the PCI bus, detects the four
 * drives its two channels may have, and moves their sectors by PIO
 * (`pio.c`) or bus master DMA (`dma.c`). Minor numbers are 64 per drive:
 * 0 is the whole disk, 1 to 4 its primary partitions, from the MBR. Reads
 * and writes go through the block cache (`blkcache.c`). One command at a
 * time runs on a channel. Only ATA hard disks are supported: not ATAPI
 * (CD-ROMs), and not SATA's own interface.
 */

#include <core/system.h>
#include <core/module.h>
#include <core/kargs.h>

#include <cpu/io.h>
#include <dev/dev.h>
#include <fs/vfs.h>
#include <fs/devfs.h>
#include <fs/posix.h>
#include <fs/mbr.h>
#include <aquila/errno.h>

#include <dev/pci.h>
#include <dev/blkcache.h>
#include <dev/irq.h>
#include <sys/sched.h>

#include <ata.h>

#define BLOCK_SIZE  512UL

static struct ata_drive drives[4];
static struct ata_channel channels[2];

/** Takes a channel for one command, waiting while another runs (a DMA
 * request sleeps). Before there are processes, nothing else runs. A
 * signal doesn't end the wait: a transfer is never left halfway. */
static void channel_acquire(struct ata_channel *ch)
{
    while (ch->busy && sched_running && curthread)
        waitq_sleep_unint(&ch->wait);

    ch->busy = 1;
}

/** Gives a channel back, and wakes up the requests waiting for it. */
static void channel_release(struct ata_channel *ch)
{
    ch->busy = 0;
    waitq_wakeup_nopoll(&ch->wait);
}

/** Reads sectors from a drive, for the block cache: the channel is held
 * for the command only, and the buffer is the cache's. */
static ssize_t drive_read(void *_drive, uint64_t lba, size_t count, void *buf)
{
    struct ata_drive *drive = _drive;

    channel_acquire(drive->channel);
    ssize_t ret = drive->read(drive, lba, count, buf);
    channel_release(drive->channel);

    return ret;
}

/** Writes sectors to a drive, for the block cache. */
static ssize_t drive_write(void *_drive, uint64_t lba, size_t count, void *buf)
{
    struct ata_drive *drive = _drive;

    channel_acquire(drive->channel);
    ssize_t ret = drive->write(drive, lba, count, buf);
    channel_release(drive->channel);

    return ret;
}

/** Handles a channel's interrupt. */
static void ata_irq(void *channel)
{
    dma_irq(channel);
}

/* the drive and mode last written to the drive select register, so that
 * selecting the same again is skipped; a reset forgets them */
static struct ata_drive *last_selected_drive = NULL;
static uint32_t last_selected_mode = -1;

/**
 * Resets both drives on a drive's channel, and waits until they are done.
 *
 * @param drive  a drive on the channel
 */
void ata_reset(struct ata_drive *drive)
{
    /* select the master first, so that the status is its (an absent slave
     * would read 0, which hides BSY) */
    io_out8(&drive->base, ATA_REG_HDDEVSEL, 0xA0);
    ata_wait(drive);

    io_out8(&drive->ctrl, 0, ATA_CMD_RESET);
    ata_wait(drive);
    io_out8(&drive->ctrl, 0, 0);
    ata_wait(drive);

    for (int i = 0; i < ATA_TIMEOUT; ++i) {
        if (!(io_in8(&drive->ctrl, 0) & ATA_STATUS_BSY))
            break;
    }

    /* a reset changes the drive selection: the next select must happen */
    last_selected_drive = NULL;
    last_selected_mode  = -1;
}

/**
 * Selects a drive on its channel, for an addressing mode, if it isn't
 * selected so already, and waits for the drive to take it.
 *
 * @param drive  the drive
 * @param mode   the addressing mode (`ATA_MODE_*`), or 0 to detect it
 */
void ata_select_drive(struct ata_drive *drive, uint32_t mode)
{
    if (drive != last_selected_drive || mode != last_selected_mode) {
        if (mode & ATA_MODE_LBA48)
            io_out8(&drive->base, ATA_REG_HDDEVSEL, 0xE0 | (drive->slave << 4) | (mode & 0xF));
        else if (mode & ATA_MODE_LBA28)
            io_out8(&drive->base, ATA_REG_HDDEVSEL, 0xE0 | (drive->slave << 4));
        else
            io_out8(&drive->base, ATA_REG_HDDEVSEL, 0xA0 | (drive->slave << 4) | (mode & 0xF));

        ata_wait(drive);
        ata_wait(drive);
        ata_wait(drive);
        ata_wait(drive);
        ata_wait(drive);

        last_selected_drive = drive;
        last_selected_mode  = mode;
    }
}

/**
 * Writes the registers of a command on `count` sectors from `lba`: the
 * drive, the address and the count. The command itself is sent after.
 *
 * @param drive  the drive
 * @param lba    the first sector
 * @param count  how many sectors
 */
void ata_setup(struct ata_drive *drive, uint64_t lba, size_t count)
{
    ata_select_drive(drive, drive->mode);

    /* the features register (the error register's port) is 0 */
    io_out8(&drive->base, ATA_REG_ERROR, 0);

    /* the sector count and the address: with LBA48, the high bytes first,
     * then the low ones, through the same registers */
    if (drive->mode == ATA_MODE_LBA48) {
        io_out8(&drive->base, ATA_REG_SECCOUNT0, (count >> 8) & 0xFF);
        io_out8(&drive->base, ATA_REG_LBA0, (uint8_t) (lba >> (8 * 3)));
        io_out8(&drive->base, ATA_REG_LBA1, (uint8_t) (lba >> (8 * 4)));
        io_out8(&drive->base, ATA_REG_LBA2, (uint8_t) (lba >> (8 * 5)));
    } else {
        /* LBA28 has bits 24 to 27 of the address in the drive select
         * register */
        io_out8(&drive->base, ATA_REG_HDDEVSEL,
                0xE0 | (drive->slave << 4) | ((lba >> 24) & 0xF));
    }

    io_out8(&drive->base, ATA_REG_SECCOUNT0, count & 0xFF);
    io_out8(&drive->base, ATA_REG_LBA0, (uint8_t) (lba >> (8 * 0)));
    io_out8(&drive->base, ATA_REG_LBA1, (uint8_t) (lba >> (8 * 1)));
    io_out8(&drive->base, ATA_REG_LBA2, (uint8_t) (lba >> (8 * 2)));
}

/* where the MBR is read into */
static char read_buf[BLOCK_SIZE] __aligned(16);

/**
 * Finds partition `part` of a drive (1 to 4; 0 is the whole disk): its
 * first sector and how many sectors it has, from the MBR, which is read
 * once. Returns 0, `-ENXIO` if the MBR has no partition there (no
 * signature, an empty entry, or one past the disk's end), or `-EIO` if
 * the MBR can't be read.
 */
static int ata_partition(struct ata_drive *drive, size_t part, uint64_t *start, uint64_t *nsectors)
{
    if (!part) {
        *start = 0;
        *nsectors = drive->max_lba;
        return 0;
    }

    part -= 1;

    if (!drive->psize[part]) {
        if (drive->read(drive, 0, 1, read_buf) < 0)
            return -EIO;

        struct mbr *mbr = (struct mbr *) read_buf;
        struct mbr_part *e = &mbr->ptab[part];

        /* the CHS fields aren't used: every disk since the 90s has the
         * LBA ones */
        if (mbr->boot_signature != 0xAA55 || !e->type || !e->sectors_count ||
            (uint64_t) e->start_lba + e->sectors_count > drive->max_lba)
            return -ENXIO;

        drive->poff[part]  = e->start_lba;
        drive->psize[part] = e->sectors_count;
    }

    *start = drive->poff[part];
    *nsectors = drive->psize[part];
    return 0;
}

/** Finds the drive of a device number, and where its partition is on
 * it. Returns 0 or a negative errno (`-ENXIO` if there is no such drive
 * or partition). */
static int ata_locate(struct devid *dd, int write, struct ata_drive **ref,
                      uint64_t *start, uint64_t *nsectors)
{
    /* 64 minors per drive: the drive's number, then the partition's */
    size_t drive_id  = dd->minor / 64;
    size_t partition = dd->minor % 64;

    if (drive_id >= 4 || partition > 4)
        return -ENXIO;

    struct ata_drive *drive = &drives[drive_id];

    if (!(write ? drive->write : drive->read))
        return -ENXIO;

    channel_acquire(drive->channel);
    int err = ata_partition(drive, partition, start, nsectors);
    channel_release(drive->channel);

    *ref = drive;
    return err;
}

/** Reads sectors of a partition, from sector `offset` on, through the
 * block cache. Returns how many were read (none past the partition's
 * end), or a negative errno. */
static ssize_t ata_read(struct devid *dd, off_t offset, size_t size, void *buf)
{
    struct ata_drive *drive;
    uint64_t start, nsectors;
    int err;

    if ((err = ata_locate(dd, 0, &drive, &start, &nsectors)))
        return err;

    if ((uint64_t) offset >= nsectors)
        return 0;

    size = MIN(size, nsectors - offset);
    err = blkcache_read(drive, start + offset, size, buf, drive->max_lba, drive_read);

    return err < 0 ? err : (ssize_t) size;
}

/** Writes sectors of a partition, from sector `offset` on, through the
 * block cache. Returns how many were written (none past the partition's
 * end: `-ENOSPC`), or a negative errno. */
static ssize_t ata_write(struct devid *dd, off_t offset, size_t size, void *buf)
{
    struct ata_drive *drive;
    uint64_t start, nsectors;
    int err;

    if ((err = ata_locate(dd, 1, &drive, &start, &nsectors)))
        return err;

    if ((uint64_t) offset >= nsectors)
        return size ? -ENOSPC : 0;

    size = MIN(size, nsectors - offset);
    err = blkcache_write(drive, start + offset, size, buf, drive_write);

    return err < 0 ? err : (ssize_t) size;
}

/** Returns whether the selected drive is there. It must be called once
 * the channel's reset is done. A floating bus (no drive on the channel)
 * reads 0xFF, and a missing drive next to a present one reads 0. */
static int ata_drive_present(struct ata_drive *drive)
{
    uint8_t status = io_in8(&drive->base, ATA_REG_STATUS);

    if (status == 0xFF || status & ATA_STATUS_BSY)
        return 0;

    /* an ATA drive says it is ready; an ATAPI drive doesn't, but sets a
     * signature */
    if (status & ATA_STATUS_DRDY)
        return 1;

    uint8_t lba1 = io_in8(&drive->base, ATA_REG_LBA1);
    uint8_t lba2 = io_in8(&drive->base, ATA_REG_LBA2);

    return (lba1 == 0x14 && lba2 == 0xEB) || (lba1 == 0x69 && lba2 == 0x96);
}

/** Detects what a drive is (`ATADEV_*`). An ATA disk's IDENTIFY reply
 * gives its size, its capabilities and its model. */
static uint8_t ata_detect_drive(struct ata_drive *drive)
{
    if (!drive->slave) {
        /* reset both drives on the channel, once */
        ata_reset(drive);
    }

    ata_select_drive(drive, 0);

    if (!ata_drive_present(drive)) {
        /* no drive there */
        drive->type = ATADEV_NOTFOUND;
        goto done;
    }

    uint8_t  type_lo = io_in8(&drive->base, ATA_REG_LBA1);
    uint8_t  type_hi = io_in8(&drive->base, ATA_REG_LBA2);
    uint16_t type    = (type_hi << 8) | type_lo;

    switch (type) {
        case 0x0000:
            type = ATADEV_PATA;
            break;
        case 0xC33C:
            type = ATADEV_SATA;
            break;
        case 0xEB14:
            type = ATADEV_PATAPI;
            break;
        case 0x9669:
            type = ATADEV_SATAPI;
            break;
        default:
            type = ATADEV_UNKOWN;
            break;
    }

    if (type == ATADEV_UNKOWN) {
        return ATADEV_UNKOWN;
    }

    drive->type = type;

    uint8_t identify_command = 0;

    if (type == ATADEV_PATAPI || type == ATADEV_SATAPI) {
        identify_command = ATA_CMD_IDENTIFY_PACKET;
    } else {
        identify_command = ATA_CMD_IDENTIFY;
    }

    char ide_ident[512];
    memset(ide_ident, 0, sizeof(ide_ident));

    io_out8(&drive->base, ATA_REG_CMD, identify_command);

    if (io_in8(&drive->base, ATA_REG_STATUS) == 0 || ata_poll(drive, 1)) {
        /* the drive didn't answer IDENTIFY */
        drive->type = ATADEV_NOTFOUND;
        goto done;
    }

    for (int i = 0; i < 512; i += 2) {
        uint16_t x = io_in16(&drive->base, ATA_REG_DATA);
        ide_ident[i+0] = x & 0xFFFF;
        ide_ident[i+1] = (x >> 8) & 0xFFFF;
    }

    drive->signature    = *(uint16_t *) (ide_ident + ATA_IDENT_DEVICETYPE);
    drive->capabilities = *(uint16_t *) (ide_ident + ATA_IDENT_CAPABILITIES);
    drive->command_sets = *(uint32_t *) (ide_ident + ATA_IDENT_COMMANDSETS);

    if (drive->command_sets & (1 << 26)) {
        /* 48-bit LBA */
        drive->max_lba = *(uint64_t *) (ide_ident + ATA_IDENT_MAX_LBA_EXT);
        drive->mode    = ATA_MODE_LBA48;
    } else {
        /* 28-bit LBA */
        drive->max_lba = *(uint32_t *) (ide_ident + ATA_IDENT_MAX_LBA);
        drive->mode    = ATA_MODE_LBA28;
    }

    for (int i = 0; i < 40; i += 2) {
        drive->model[i + 0] = ide_ident[ATA_IDENT_MODEL + i + 1];
        drive->model[i + 1] = ide_ident[ATA_IDENT_MODEL + i + 0];
    }
    drive->model[40] = 0;

    printk("ata%d: signature: 0x%x\n", drive->id, drive->signature);
    printk("ata%d: capabilities: 0x%x\n", drive->id, drive->capabilities);
    printk("ata%d: command Sets: 0x%x\n", drive->id, drive->command_sets);
    printk("ata%d: max LBA: 0x%lx\n", drive->id, drive->max_lba);
    printk("ata%d: model: %s\n", drive->id, drive->model);
    
done:
    return drive->type;
}

/** Has every drive write what its cache holds to the disk, for `sync()`.
 * Returns 0, or `-EIO` if a drive failed. */
static int ata_sync(void)
{
    int err = 0;

    for (int i = 0; i < 4; ++i) {
        if (!drives[i].flush)
            continue;

        channel_acquire(drives[i].channel);
        if (drives[i].flush(&drives[i]))
            err = -EIO;
        channel_release(drives[i].channel);
    }

    return err;
}

/** Returns the block size: one sector, 512 bytes. */
static size_t ata_getbs(struct devid *dd __unused)
{
    return BLOCK_SIZE;
}

/** Finds the IDE controller, detects its drives, sets them up, and
 * registers the disks as block device 3. */
static int ata_probe()
{
    struct pci_dev ide;
    int count;

    printk("ata: scanning PCI bus for IDE controller(s)\n");

    count = pci_scan_device(0x01, 0x01, &ide, 1);

    if (!count) {
        printk("ata: no IDE controller(s) found\n");
        return -ENODEV;
    }

    if (count) {
        printk("ata: found IDE controller: bus: 0x%x, dev: 0x%x, func: 0x%x\n", ide.bus, ide.dev, ide.func);

        uint32_t base, control;

        /* the primary channel: its ports from BARs 0 and 1, or the ISA's
         * in compatibility mode */
        base    = pci_read_bar(&ide, 0);
        control = pci_read_bar(&ide, 1);

        if (base == 0 || base == 1)
            base = ATA_PIO_PRI_PORT_BASE;

        if (control == 0 || control == 1)
            control = ATA_PIO_PRI_PORT_CONTROL;

        printk("ata: IDE controller primary channel: base: 0x%x, control: 0x%x\n", base, control);

        /* in native PCI mode, the channels use the controller's interrupt;
         * in compatibility mode, the ISA's IRQs 14 and 15 */
        int native = !(base == ATA_PIO_PRI_PORT_BASE);
        channels[0].base = (struct ioaddr) {.addr = base, .type = IOADDR_PORT};
        channels[0].irq  = native ? pci_irq(&ide) : 14;

        /* its master drive */
        drives[0].id        = 0;
        drives[0].base.addr = base;
        drives[0].base.type = IOADDR_PORT;
        drives[0].ctrl.addr = control;
        drives[0].ctrl.type = IOADDR_PORT;
        drives[0].slave     = 0;

        /* its slave drive */
        drives[1].id        = 1;
        drives[1].base.addr = base;
        drives[1].base.type = IOADDR_PORT;
        drives[1].ctrl.addr = control;
        drives[1].ctrl.type = IOADDR_PORT;
        drives[1].slave     = 1;

        /* the secondary channel: BARs 2 and 3 */
        base    = pci_read_bar(&ide, 2);
        control = pci_read_bar(&ide, 3);

        if (base == 0 || base == 1)
            base = ATA_PIO_SEC_PORT_BASE;

        if (control == 0 || control == 1)
            control = ATA_PIO_SEC_PORT_CONTROL;

        printk("ata: IDE controller secondary channel: base: 0x%x, control: 0x%x\n", base, control);

        channels[1].base = (struct ioaddr) {.addr = base, .type = IOADDR_PORT};
        channels[1].irq  = native ? channels[0].irq : 15;

        /* the bus master's registers (BAR 4), 8 for each channel */
        uint32_t bm = pci_read_bar(&ide, 4);

        if ((bm & 1) && (bm & ~3U)) {
            channels[0].bm = (struct ioaddr) {.addr = bm & ~3U, .type = IOADDR_PORT};
            channels[1].bm = (struct ioaddr) {.addr = (bm & ~3U) + 8, .type = IOADDR_PORT};

            /* the controller may master the bus */
            pci_reg16_write(&ide, 0x04, pci_reg16_read(&ide, 0x04) | 0x04);

            printk("ata: bus master DMA at 0x%x\n", bm & ~3U);
        }

        for (int i = 0; i < 4; ++i)
            drives[i].channel = &channels[i / 2];

        /* the secondary's master drive */
        drives[2].id        = 2;
        drives[2].base.addr = base;
        drives[2].base.type = IOADDR_PORT;
        drives[2].ctrl.addr = control;
        drives[2].ctrl.type = IOADDR_PORT;
        drives[2].slave     = 0;

        /* the secondary's slave drive */
        drives[3].id        = 3;
        drives[3].base.addr = base;
        drives[3].base.type = IOADDR_PORT;
        drives[3].ctrl.addr = control;
        drives[3].ctrl.type = IOADDR_PORT;
        drives[3].slave     = 1;
    }

    for (int i = 0; i < 4 * count; ++i) {
        uint8_t type = ata_detect_drive(&drives[i]);

        switch (type) {
            case ATADEV_PATA:
                pio_init(&drives[i]);
                /* DMA if possible, unless the ata.nodma argument says PIO */
                printk("ata%d: initializing ATA device (%s)\n", i,
                        !kargs_get("ata.nodma", NULL) || dma_init(&drives[i]) ? "PIO" : "DMA");
                break;
            case ATADEV_SATA:
                printk("ata%d: SATA is not supported\n", i);
                break;
            case ATADEV_PATAPI:
                printk("ata%d: PATAPI is not supported\n", i);
                break;
            case ATADEV_SATAPI:
                printk("ata%d: SATAPI is not supported\n", i);
                break;
            case ATADEV_NOTFOUND:
                break;
            default:
                break;
        }
    }

    /* the channels' interrupts: they end DMA transfers, and the others
     * are acknowledged */
    if (count) {
        irq_install(channels[0].irq, ata_irq, &channels[0]);
        irq_install(channels[1].irq, ata_irq, &channels[1]);
    }

    kdev_blkdev_register(3, &atadev);
    return 0;
}

struct dev atadev = {
    .name  = "ata",
    .probe = ata_probe,
    .bread  = ata_read,
    .bwrite = ata_write,
    .getbs = ata_getbs,
    .sync  = ata_sync,

    .fops = {
        .open  = posix_file_open,
        .read  = posix_file_read,
        .write = posix_file_write,
    },
};

MODULE_INIT(ata, ata_probe, NULL)
