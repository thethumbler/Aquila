#ifndef _ATA_H
#define _ATA_H

/*
 * The ATA driver's own definitions: the drives' registers, commands and
 * status bits, its structures for channels and drives, and the helpers
 * that ata.c, pio.c and dma.c share.
 */

/* the kinds of drive that detection finds */
#define ATADEV_UNKOWN               0x000
#define ATADEV_NOTFOUND             0x001
#define ATADEV_PATA                 0x002
#define ATADEV_SATA                 0x003
#define ATADEV_PATAPI               0x004
#define ATADEV_SATAPI               0x005

/* the channels' ports in compatibility mode, the ISA's */
#define ATA_PIO_PRI_PORT_BASE       0x1F0
#define ATA_PIO_PRI_PORT_CONTROL    0x3F6
#define ATA_PIO_SEC_PORT_BASE       0x170
#define ATA_PIO_SEC_PORT_CONTROL    0x376

/* the ATA registers, from the channel's base */
#define ATA_REG_DATA                0x00
#define ATA_REG_ERROR               0x01
#define ATA_REG_FEATURES            0x01
#define ATA_REG_SECCOUNT0           0x02
#define ATA_REG_LBA0                0x03
#define ATA_REG_LBA1                0x04
#define ATA_REG_LBA2                0x05
#define ATA_REG_HDDEVSEL            0x06
#define ATA_REG_CMD                 0x07
#define ATA_REG_STATUS              0x07
#define ATA_REG_SECCOUNT1           0x08
#define ATA_REG_LBA3                0x09
#define ATA_REG_LBA4                0x0A
#define ATA_REG_LBA5                0x0B
#define ATA_REG_CONTROL             0x0C
#define ATA_REG_ALTSTATUS           0x0C
#define ATA_REG_DEVADDRESS          0x0D

/* the ATA commands */
#define ATA_CMD_RESET               0x04
#define ATA_CMD_READ_SECTORS        0x20
#define ATA_CMD_READ_SECTORS_EXT    0x24
#define ATA_CMD_WRITE_SECTORS       0x30
#define ATA_CMD_WRITE_SECTORS_EXT   0x34
#define ATA_CMD_READ_DMA            0xC8
#define ATA_CMD_READ_DMA_EXT        0x25
#define ATA_CMD_WRITE_DMA           0xCA
#define ATA_CMD_WRITE_DMA_EXT       0x35
#define ATA_CMD_CACHE_FLUSH         0xE7
#define ATA_CMD_CACHE_FLUSH_EXT     0xEA
#define ATA_CMD_PACKET              0xA0
#define ATA_CMD_IDENTIFY_PACKET     0xA1
#define ATA_CMD_IDENTIFY            0xEC

#define ATA_DRIVE_LBA48             0x40

/* the status register's bits */
#define ATA_STATUS_BSY              0x80    /* busy */
#define ATA_STATUS_DRDY             0x40    /* drive ready */
#define ATA_STATUS_DF               0x20    /* drive write fault */
#define ATA_STATUS_DSC              0x10    /* drive seek complete */
#define ATA_STATUS_DRQ              0x08    /* ready to transfer data */
#define ATA_STATUS_CORR             0x04    /* corrected data */
#define ATA_STATUS_IDX              0x02    /* index */
#define ATA_STATUS_ERR              0x01    /* error */

/* the error register's bits */
#define ATA_ERROR_BBK               0x080   /* bad block */
#define ATA_ERROR_UNC               0x040   /* uncorrectable data */
#define ATA_ERROR_MC                0x020   /* media changed */
#define ATA_ERROR_IDNF              0x010   /* ID mark not found */
#define ATA_ERROR_MCR               0x008   /* media change request */
#define ATA_ERROR_ABRT              0x004   /* command aborted */
#define ATA_ERROR_TK0NF             0x002   /* track 0 not found */
#define ATA_ERROR_AMNF              0x001   /* no address mark */

/* where things are in the IDENTIFY command's reply, in bytes */
#define ATA_IDENT_DEVICETYPE        0
#define ATA_IDENT_CYLINDERS         2
#define ATA_IDENT_HEADS             6
#define ATA_IDENT_SECTORS           12
#define ATA_IDENT_SERIAL            20
#define ATA_IDENT_MODEL             54
#define ATA_IDENT_CAPABILITIES      98
#define ATA_IDENT_FIELDVALID        106
#define ATA_IDENT_MAX_LBA           120
#define ATA_IDENT_COMMANDSETS       164
#define ATA_IDENT_MAX_LBA_EXT       200

/* the capabilities word's bits */
#define ATA_CAP_DMA                 0x100
#define ATA_CAP_LBA                 0x200

/* how a drive's sectors are addressed */
#define ATA_MODE_CHS                0x10
#define ATA_MODE_LBA28              0x20
#define ATA_MODE_LBA48              0x40

#include <ds/queue.h>
#include <aquila/errno.h>

/** An IDE channel, with the master and slave drives on it. */
struct ata_channel {
    /** the command block's registers, and the bus master's for DMA (whose
     *  address is 0 if the controller has none) */
    struct ioaddr base;
    struct ioaddr bm;
    int irq;

    /** one command at a time: whether one is running, and the requests
     *  waiting for the channel */
    int busy;
    struct waitq wait;

    /** the DMA transfer waiting for the interrupt, and what it found */
    struct waitq irq_wait;
    volatile int done;
    uint8_t bm_status, status;
};

/** A drive on a channel. */
struct ata_drive {
    struct ioaddr base;
    struct ioaddr ctrl;
    struct ata_channel *channel;

    uint8_t  id;
    uint8_t  mode;
    uint16_t type;
    uint8_t  slave;
    size_t   poff[4];       /* each partition's first sector */
    size_t   psize[4];      /* each partition's size in sectors, 0 until
                             * the MBR was read */


    uint16_t signature;
    uint16_t capabilities;
    uint32_t command_sets;
    uint64_t max_lba;
    char     model[41];

    /* how sectors are moved: by PIO (pio.c) or DMA (dma.c) */
    ssize_t (*read) (struct ata_drive *drive, uint64_t lba, size_t count, void *buf);
    ssize_t (*write)(struct ata_drive *drive, uint64_t lba, size_t count, void *buf);
    int     (*flush)(struct ata_drive *drive);
};

/** Returns the name of an error, from the error register. */
static const char *ata_error_string(uint8_t err)
{
    switch (err) {
        case ATA_ERROR_BBK:   return "Bad block";
        case ATA_ERROR_UNC:   return "Uncorrectable data";
        case ATA_ERROR_MC:    return "Media changed";
        case ATA_ERROR_IDNF:  return "ID mark not found";
        case ATA_ERROR_MCR:   return "Media change request";
        case ATA_ERROR_ABRT:  return "Command aborted";
        case ATA_ERROR_TK0NF: return "Track 0 not found";
        case ATA_ERROR_AMNF:  return "No address mark";
        default:              return "Unkown";
    }
}

/** Waits about 400 ns, as a drive needs after a command, by reading the
 * alternate status register four times. */
static void ata_wait(struct ata_drive *drive)
{
    /* the alternate status register is at offset 0 of the control block */
    for (int i = 0; i < 4; ++i)
        io_in8(&drive->ctrl, 0);
}

/* the most status reads while waiting for a drive: a drive is busy for
 * milliseconds, and one that never answers mustn't be waited for forever,
 * with interrupts off */
#define ATA_TIMEOUT     1000000

/**
 * Waits until a drive is done with what it was doing (BSY clears). With
 * `advanced_check`, it also checks that the drive is ready for data (DRQ)
 * and not in error. Returns 0, `-ETIMEDOUT` if the drive stays busy,
 * `-EAGAIN` if it aborted the command (which may be tried again), or
 * `-EIO` for any other error.
 */
static int ata_poll(struct ata_drive *drive, int advanced_check)
{
    ata_wait(drive);

    uint8_t s = ATA_STATUS_BSY;

    for (int i = 0; i < ATA_TIMEOUT && (s & ATA_STATUS_BSY); ++i)
        s = io_in8(&drive->base, ATA_REG_STATUS);

    if (s & ATA_STATUS_BSY) {
        printk("ata: drive busy, not answering\n");
        return -ETIMEDOUT;
    }

    if (advanced_check) {
        if ((s & ATA_STATUS_ERR) || (s & ATA_STATUS_DF)) {
            ata_wait(drive);
            uint8_t err = io_in8(&drive->base, ATA_REG_ERROR);
            printk("ata: drive error: %s\n", ata_error_string(err));
            return err == ATA_ERROR_ABRT ? -EAGAIN : -EIO;
        }

        if (!(s & ATA_STATUS_DRQ)) {
            printk("ata: drive error: DRQ not set\n");
            return -EIO;
        }
    }

    return 0;
}

/* ata.c: resets a drive's channel */
void ata_reset(struct ata_drive *drive);

/* ata.c: selects a drive on its channel, for an addressing mode */
void ata_select_drive(struct ata_drive *drive, uint32_t mode);

/* ata.c: writes the sectors' address and count for the next command */
void ata_setup(struct ata_drive *drive, uint64_t lba, size_t count);

/* pio.c: sets a drive up for PIO */
void pio_init(struct ata_drive *drive);

/* dma.c: sets a drive up for DMA, if it and its controller can */
int  dma_init(struct ata_drive *drive);

/* dma.c: handles a channel's interrupt */
void dma_irq(struct ata_channel *channel);

#endif
