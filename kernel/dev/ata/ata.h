#ifndef _ATA_H
#define _ATA_H

/* definitions used in atadev driver */
#define ATADEV_UNKOWN               0x000
#define ATADEV_NOTFOUND             0x001
#define ATADEV_PATA                 0x002
#define ATADEV_SATA                 0x003
#define ATADEV_PATAPI               0x004
#define ATADEV_SATAPI               0x005

/* default ports */
#define ATA_PIO_PRI_PORT_BASE       0x1F0
#define ATA_PIO_PRI_PORT_CONTROL    0x3F6
#define ATA_PIO_SEC_PORT_BASE       0x170
#define ATA_PIO_SEC_PORT_CONTROL    0x376

/* ATA registers */
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

/* ATA commands */
#define ATA_CMD_RESET               0x08
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

#define ATA_STATUS_BSY              0x80    /* Busy */
#define ATA_STATUS_DRDY             0x40    /* Drive ready */
#define ATA_STATUS_DF               0x20    /* Drive write fault */
#define ATA_STATUS_DSC              0x10    /* Drive seek complete */
#define ATA_STATUS_DRQ              0x08    /* Data request ready */
#define ATA_STATUS_CORR             0x04    /* Corrected data */
#define ATA_STATUS_IDX              0x02    /* Inlex */
#define ATA_STATUS_ERR              0x01    /* Error */

#define ATA_ERROR_BBK               0x080   /* Bad block */
#define ATA_ERROR_UNC               0x040   /* Uncorrectable data */
#define ATA_ERROR_MC                0x020   /* Media changed */
#define ATA_ERROR_IDNF              0x010   /* ID mark not found */
#define ATA_ERROR_MCR               0x008   /* Media change request */
#define ATA_ERROR_ABRT              0x004   /* Command aborted */
#define ATA_ERROR_TK0NF             0x002   /* Track 0 not found */
#define ATA_ERROR_AMNF              0x001   /* No address mark */


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

#define ATA_CAP_LBA                 0x200

#define ATA_MODE_CHS                0x10
#define ATA_MODE_LBA28              0x20
#define ATA_MODE_LBA48              0x40

struct ata_drive {
    struct ioaddr base;
    struct ioaddr ctrl;

    uint8_t  id;
    uint8_t  mode;
    uint16_t type;
    uint8_t  slave;
    size_t   poff[4];


    uint16_t signature;
    uint16_t capabilities;
    uint32_t command_sets;
    uint64_t max_lba;
    char     model[41];

    ssize_t (*read) (struct ata_drive *drive, uint64_t lba, size_t count, void *buf);
    ssize_t (*write)(struct ata_drive *drive, uint64_t lba, size_t count, void *buf);
};

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

static void ata_wait(struct ata_drive *drive)
{
    for (int i = 0; i < 4; ++i)
        io_in8(&drive->base, ATA_REG_ALTSTATUS);
}

static int ata_poll(struct ata_drive *drive, int advanced_check)
{
    ata_wait(drive);

    uint8_t s;

    /* Wait until busy clears */
    while ((s = io_in8(&drive->base, ATA_REG_STATUS)) & ATA_STATUS_BSY);

    if (advanced_check) {
        if ((s & ATA_STATUS_ERR) || (s & ATA_STATUS_DF)) {
            ata_wait(drive);
            uint8_t err = io_in8(&drive->base, ATA_REG_ERROR);
            printk("ata: drive error: %s\n", ata_error_string(err));
            return -err;
        }

        if (!(s & ATA_STATUS_DRQ)) {
            printk("ata: drive error: DRQ not set\n");
            return -1;
        }
    }

    return 0;
}

static void ata_soft_reset(struct ata_drive *drive)
{
    io_out8(&drive->ctrl, 0, ATA_CMD_RESET);
    ata_wait(drive);
    io_out8(&drive->ctrl, 0, 0);
}

/* ata.c */
void ata_select_drive(struct ata_drive *drive, uint32_t mode);

/* pio.c */
void pio_init(struct ata_drive *drive);

#endif
