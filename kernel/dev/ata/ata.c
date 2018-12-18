#include <core/system.h>
#include <cpu/io.h>
#include <dev/dev.h>
#include <fs/vfs.h>
#include <fs/devfs.h>
#include <fs/posix.h>
#include <fs/mbr.h>
#include <bits/errno.h>

#include <dev/pci.h>

#include <ata.h>

#define BLOCK_SIZE  512UL

static struct ata_drive drives[4] = {0};

void ata_select_drive(struct ata_drive *drive, uint32_t mode)
{
    static struct ata_drive *last_selected_drive = NULL;
    static uint32_t last_selected_mode = -1;

    if (drive != last_selected_drive || mode != last_selected_mode) {
        if (mode & ATA_MODE_LBA48)
            //io_out8(&drive->base, ATA_REG_HDDEVSEL, ATA_DRIVE_LBA48 | (drive->slave << 4) | (mode & 0xF));
            io_out8(&drive->base, ATA_REG_HDDEVSEL, 0xE0 | (drive->slave << 4) | (mode & 0xF));
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

static char read_buf[BLOCK_SIZE] __aligned(16);
static size_t ata_partition_offset(struct ata_drive *drive, size_t part)
{
    if (!part) /* Whole Disk */
        return 0;

    part -= 1;

    if (drive->poff[part] != 0)
        return drive->poff[part];

    struct mbr *mbr;

    drive->read(drive, 0, 1, read_buf);
    mbr = (struct mbr *) read_buf;

    /* TODO Support CHS */
    return drive->poff[part] = mbr->ptab[part].start_lba;
}

static ssize_t ata_read(struct devid *dd, off_t offset, size_t size, void *buf)
{
    printk("ata_read(dd=%p, offset=%d, size=%d, buf=%p)\n", dd, offset, size, buf);

    /* Get drive and parition numbers */
    size_t drive_id  = dd->minor / 64;
    size_t partition = dd->minor % 64;

    struct ata_drive *drive = &drives[drive_id];

    offset += ata_partition_offset(drive, partition);

    return drive->read(drive, offset, size, buf);
}

static ssize_t ata_write(struct devid *dd, off_t offset, size_t size, void *buf)
{
    /* Get drive and parition numbers */
    size_t drive_id  = dd->minor / 64;
    size_t partition = dd->minor % 64;

    struct ata_drive *drive = &drives[drive_id];

    offset += ata_partition_offset(drive, partition);

    return drive->write(drive, offset, size, buf);
}

uint8_t ata_detect_drive(struct ata_drive *drive)
{
    ata_select_drive(drive, 0);

    ata_poll(drive, 0);
    uint8_t status = io_in8(&drive->base, ATA_REG_STATUS);

    if (!status) {
        /* No Device, bail */
        drive->type = ATADEV_NOTFOUND;
        goto done;
    }

    uint8_t  type_low  = io_in8(&drive->base, ATA_REG_LBA1);
    uint8_t  type_high = io_in8(&drive->base, ATA_REG_LBA2);
    uint16_t type      = (type_high << 8) | type_low;

    switch (type) {
        case 0x0000:
            type = ATADEV_PATA;
            break;
        case 0xEB14:
            type = ATADEV_PATAPI;
            break;
        case 0x9669:
            type = ATADEV_SATAPI;
            break;
        case 0xC33C:
            type = ATADEV_SATA;
            break;
        default:
            type = ATADEV_UNKOWN;
            break;
    }

    drive->type = type;
    if (type == ATADEV_UNKOWN) {
        return ATADEV_UNKOWN;
    }

    if (type == ATADEV_PATAPI || type == ATADEV_SATAPI) {
        /* TODO */
    } else {
        char ide_ident[512];
        memset(ide_ident, 0, sizeof(ide_ident));

        io_out8(&drive->base, ATA_REG_CMD, ATA_CMD_IDENTIFY);
        ata_poll(drive, 1);

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
            uint32_t high  = *(uint32_t *) (ide_ident + ATA_IDENT_MAX_LBA);
            uint32_t low   = *(uint32_t *) (ide_ident + ATA_IDENT_MAX_LBA_EXT);

            drive->max_lba = ((uint64_t) high << 32) | low;
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

        printk("ata%d: Signature: 0x%x\n", drive->id, drive->signature);
        printk("ata%d: Capabilities: 0x%x\n", drive->id, drive->capabilities);
        printk("ata%d: Command Sets: 0x%x\n", drive->id, drive->command_sets);
        printk("ata%d: Max LBA: 0x%x\n", drive->id, drive->max_lba);
        printk("ata%d: Model: %s\n", drive->id, drive->model);
    }
    
done:
    return drive->type;
}

size_t ata_getbs(struct devid *dd __unused)
{
    return BLOCK_SIZE;
}

int ata_probe()
{
    struct pci_dev ide;
    int count;

    printk("ata: Scanning PCI bus for IDE controller\n");

    if (!(count = pci_scan_device(0x01, 0x01, &ide, 1))) {
        printk("ata: No IDE controller(s) found\n");
        return -1;
    }

    //printk("ata: found %d IDE controller(s)\n", count);
    if (count) {
        printk("ata: Found IDE controller (Bus: %x, Dev: %x, Func: %x)\n", ide.bus, ide.dev, ide.func);

        uint32_t base, control;

        /* Primary Channel */
        base    = pci_read_bar(&ide, 0);
        control = pci_read_bar(&ide, 1);

        if (base == 0 || base == 1)
            base = ATA_PIO_PRI_PORT_BASE;

        if (control == 0 || control == 1)
            control = ATA_PIO_PRI_PORT_CONTROL;

        printk("ata: IDE controller primary channel: (Base: 0x%x, Control: 0x%x)\n", base, control);

        /* Master Device */
        drives[0].id        = 0;
        drives[0].base.addr = base;
        drives[0].base.type = IOADDR_PORT;
        drives[0].ctrl.addr = control;
        drives[0].ctrl.type = IOADDR_PORT;
        drives[0].slave     = 0;

        /* Slave Device */
        drives[1].id        = 1;
        drives[1].base.addr = base;
        drives[1].base.type = IOADDR_PORT;
        drives[1].ctrl.addr = control;
        drives[1].ctrl.type = IOADDR_PORT;
        drives[1].slave     = 1;

        /* Secondary Channel */
        base    = pci_read_bar(&ide, 2);
        control = pci_read_bar(&ide, 3);

        if (base == 0 || base == 1)
            base = ATA_PIO_SEC_PORT_BASE;

        if (control == 0 || control == 1)
            control = ATA_PIO_SEC_PORT_CONTROL;

        printk("ata: IDE controller secondary channel: (Base: 0x%x, Control: 0x%x)\n", base, control);

        /* Master Device */
        drives[2].id        = 2;
        drives[2].base.addr = base;
        drives[2].base.type = IOADDR_PORT;
        drives[2].ctrl.addr = control;
        drives[2].ctrl.type = IOADDR_PORT;
        drives[2].slave     = 0;

        /* Slave Device */
        drives[3].id        = 2;
        drives[3].base.addr = base;
        drives[3].base.type = IOADDR_PORT;
        drives[3].ctrl.addr = control;
        drives[3].ctrl.type = IOADDR_PORT;
        drives[3].slave     = 1;
    }

    for (int i = 0; i < 4*count; ++i) {
        /* disable interrupts */
        io_out8(&drives[i].base, ATA_REG_CONTROL, 0x2);

        uint8_t type = ata_detect_drive(&drives[i]);
        switch (type) {
            case ATADEV_PATA:
                printk("ata%d: Initializing ATA device (PIO)\n", i);
                pio_init(&drives[i]);
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
                printk("ata%d: Unkown ATA type %d\n", i, drives[i].type);
                break;
        }
    }

    kdev_blkdev_register(3, &atadev);
    return 0;
}

struct dev atadev = {
    .name  = "ata",
    .probe = ata_probe,
    .read  = ata_read,
    .write = ata_write,
    .getbs = ata_getbs,

    .fops = {
        .open  = posix_file_open,
        .read  = posix_file_read,
        .write = posix_file_write,
    },
};

MODULE_INIT(ata, ata_probe, NULL)
