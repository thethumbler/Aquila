#include <core/system.h>
#include <core/panic.h>
#include <cpu/io.h>
#include <dev/dev.h>
#include <dev/ata.h>
#include <fs/vfs.h>
#include <fs/devfs.h>
#include <fs/mbr.h>
#include <bits/errno.h>

#include <dev/pci.h>

static struct ata_device {
    uint16_t base;
    uint16_t ctrl;
    uint8_t  slave;
} devices[4] = {0};

void ata_wait(struct ata_device *dev)
{
    for (int i = 0; i < 4; ++i)
        inb(dev->base + ATA_PORT_CMD);
}

void ata_soft_reset(struct ata_device *dev)
{
    outb(dev->ctrl, ATA_CMD_RESET);
    ata_wait(dev);
    outb(dev->ctrl, 0);
}

void ata_poll(struct ata_device *dev, int advanced_check)
{
    ata_wait(dev);

    uint8_t s;
    /* Wait until busy clears */
    while ((s = inb(dev->base + ATA_PORT_CMD)) & ATA_STATUS_BSY);

    if (advanced_check) {
        if ((s & ATA_STATUS_ERR) || (s & ATA_STATUS_DF)) {
            ata_wait(dev);
            uint8_t err = inb(dev->base + ATA_PORT_ERROR);
            printk("DRIVE ERROR! %d\n", err);
            panic("");
        }

        if (!(s & ATA_STATUS_DRQ)) {
            panic("DRQ NOT SET\n");
        }
    }
}

static struct ata_device *__last_selected_dev = NULL;
static void ata_select_device(struct ata_device *dev)
{
    if (dev != __last_selected_dev) {
        outb(dev->base + ATA_PORT_DRIVE, ATA_DRIVE_LBA48 | (dev->slave << 4));
        ata_wait(dev);
        __last_selected_dev = dev;
    }
}

static ssize_t ata_read_sectors(struct ata_device *dev, uint64_t lba, size_t count, void *buf)
{
    //printk("ata_read_sectors(dev=%p, lba=%x, count=%d, buf=%p)\n", dev, (uint32_t) lba, count, buf);    /* XXX */
    ata_select_device(dev);

    /* Send NULL byte to error port */
    outb(dev->base + ATA_PORT_ERROR, 0);

    /* Send sectors count and LBA */
    outb(dev->base + ATA_PORT_COUNT, (count >> 8) & 0xFF);
    outb(dev->base + ATA_PORT_LBALO, (uint8_t) (lba >> (8 * 3)));
    outb(dev->base + ATA_PORT_LBAMI, (uint8_t) (lba >> (8 * 4)));
    outb(dev->base + ATA_PORT_LBAHI, (uint8_t) (lba >> (8 * 5)));
    outb(dev->base + ATA_PORT_COUNT, count & 0xFF);
    outb(dev->base + ATA_PORT_LBALO, (uint8_t) (lba >> (8 * 0)));
    outb(dev->base + ATA_PORT_LBAMI, (uint8_t) (lba >> (8 * 1)));
    outb(dev->base + ATA_PORT_LBAHI, (uint8_t) (lba >> (8 * 2)));

    /* Send read command */
    ata_wait(dev);
    outb(dev->base + ATA_PORT_CMD, ATA_CMD_READ_SECOTRS_EXT);

    while (count--) {
        ata_poll(dev, 1);
        insw(dev->base + ATA_PORT_DATA, 256, buf);
        buf += 512;
    }

    ata_poll(dev, 0);

    return 0;
}

static ssize_t ata_write_sectors(struct ata_device *dev, uint64_t lba, size_t count, void *buf)
{
    //printk("ata_write_sectors(dev=%p, lba=%x, count=%d, buf=%p)\n", dev, (uint32_t) lba, count, buf);    /* XXX */
    ata_select_device(dev);

    /* Send NULL byte to error port */
    outb(dev->base + ATA_PORT_ERROR, 0);

    /* Send sectors count and LBA */
    outb(dev->base + ATA_PORT_COUNT, (count >> 8) & 0xFF);
    outb(dev->base + ATA_PORT_LBALO, (uint8_t) (lba >> (8 * 3)));
    outb(dev->base + ATA_PORT_LBAMI, (uint8_t) (lba >> (8 * 4)));
    outb(dev->base + ATA_PORT_LBAHI, (uint8_t) (lba >> (8 * 5)));
    outb(dev->base + ATA_PORT_COUNT, count & 0xFF);
    outb(dev->base + ATA_PORT_LBALO, (uint8_t) (lba >> (8 * 0)));
    outb(dev->base + ATA_PORT_LBAMI, (uint8_t) (lba >> (8 * 1)));
    outb(dev->base + ATA_PORT_LBAHI, (uint8_t) (lba >> (8 * 2)));

    /* Send write command */
    ata_wait(dev);
    outb(dev->base + ATA_PORT_CMD, ATA_CMD_WRITE_SECOTRS_EXT);

    while (count--) {
        uint16_t *_buf = (uint16_t *) buf;

        ata_poll(dev, 1);
        for (int i = 0; i < 256; ++i)
            outw(dev->base + ATA_PORT_DATA, _buf[i]);

        buf += 512;
    }

    ata_poll(dev, 0);
    outb(dev->base + ATA_PORT_CMD, ATA_CMD_CACHE_FLUSH);
    ata_poll(dev, 0);

    return 0;
}

#define BLOCK_SIZE  512UL
static char read_buf[BLOCK_SIZE] __aligned(16);
static ssize_t ata_read(struct fs_node *node, off_t offset, size_t size, void *buf)
{
    //printk("ata_read(node=%p, offset=%x, size=%d, buf=%p)\n", node, offset, size, buf);
    ssize_t ret = 0;
    char *_buf = buf;

    struct ata_device *dev = node->p;
    offset += node->offset;

    if (offset % BLOCK_SIZE) {
        /* Read up to block boundary */
        size_t start = MIN(BLOCK_SIZE - offset % BLOCK_SIZE, size);

        if (start) {
            ata_read_sectors(dev, offset/BLOCK_SIZE, 1, read_buf);
            memcpy(_buf, read_buf + (offset % BLOCK_SIZE), start);

            ret += start;
            size -= start;
            _buf += start;
            offset += start;

            if (!size)
                return ret;
        }
    }


    /* Read whole sectors */
    size_t count = size/BLOCK_SIZE;

    while (count) {
        ata_read_sectors(dev, offset/BLOCK_SIZE, 1, read_buf);
        memcpy(_buf, read_buf, BLOCK_SIZE);

        ret    += BLOCK_SIZE;
        size   -= BLOCK_SIZE;
        _buf   += BLOCK_SIZE;
        offset += BLOCK_SIZE;
        --count;
    }

    if (!size)
        return ret;

    size_t end = size % BLOCK_SIZE;

    if (end) {
        ata_read_sectors(dev, offset/BLOCK_SIZE, 1, read_buf);
        memcpy(_buf, read_buf, end);
        ret += end;
    }

    return ret;
}

static ssize_t ata_write(struct fs_node *node, off_t offset, size_t size, void *buf)
{
    //printk("ata_write(node=%p, offset=%x, size=%d, buf=%p)\n", node, offset, size, buf);
    ssize_t ret = 0;
    char *_buf = buf;

    struct ata_device *dev = node->p;
    offset += node->offset;

    if (offset % BLOCK_SIZE) {
        /* Write up to block boundary */
        size_t start = MIN(BLOCK_SIZE - offset % BLOCK_SIZE, size);

        if (start) {
            ata_read_sectors(dev, offset/BLOCK_SIZE, 1, read_buf);
            memcpy(read_buf + (offset % BLOCK_SIZE), _buf, start);
            ata_write_sectors(dev, offset/BLOCK_SIZE, 1, read_buf);

            ret += start;
            size -= start;
            _buf += start;
            offset += start;

            if (!size)
                return ret;
        }
    }


    /* Write whole sectors */
    size_t count = size/BLOCK_SIZE;

    while (count) {
        memcpy(read_buf, _buf, BLOCK_SIZE);
        ata_write_sectors(dev, offset/BLOCK_SIZE, 1, read_buf);

        ret    += BLOCK_SIZE;
        size   -= BLOCK_SIZE;
        _buf   += BLOCK_SIZE;
        offset += BLOCK_SIZE;
        --count;
    }

    if (!size)
        return ret;

    size_t end = size % BLOCK_SIZE;

    if (end) {
        ata_read_sectors(dev, offset/BLOCK_SIZE, 1, read_buf);
        memcpy(read_buf, _buf, end);
        ata_write_sectors(dev, offset/BLOCK_SIZE, 1, read_buf);
        ret += end;
    }

    return ret;
}

uint8_t ata_detect_device(struct ata_device *dev)
{
    ata_select_device(dev);

    uint8_t status = inb(dev->base + ATA_PORT_CMD);
    if (!status) /* No Device, bail */
        return ATADEV_UNKOWN;

    uint8_t type_low  = inb(dev->base + ATA_PORT_LBAMI);
    uint8_t type_high = inb(dev->base + ATA_PORT_LBAHI);

    if (type_low == 0x14 && type_high == 0xEB)
        return ATADEV_PATAPI;
    if (type_low == 0x69 && type_high == 0x96)
        return ATADEV_SATAPI;
    if (type_low == 0 && type_high == 0)
        return ATADEV_PATA;
    if (type_low == 0x3c && type_high == 0xc3)
        return ATADEV_SATA;
    
    return ATADEV_UNKOWN;
}

static char pata_idx = 'a';
void pata_init(struct ata_device *dev)
{
    char name[] = "hda";
    name[2] = pata_idx;

    printk("Initializing Parallel ATA Device %s\n", name);
    ata_soft_reset(dev);

    vfs.create(dev_root, name);

    struct vfs_path path = (struct vfs_path) {
        .mountpoint = dev_root,
        .tokens = (char *[]) {name, NULL}
    };

	struct fs_node *hd = vfs.traverse(&path);

    hd->dev = &atadev;
    hd->p = dev;

    readmbr(hd);
}

int ata_probe()
{
    printk("ide_prope()\n");

    struct pci_dev ide;
    if (pci_scan_device(0x01, 0x01, &ide)) {
        panic("No IDE controller found\n"); /* FIXME */
    }

    devices[0].base = ATA_PIO_PORT_BASE;
    devices[0].ctrl = ATA_PIO_PORT_CONTROL;

    uint8_t type = ata_detect_device(&devices[0]);

    switch (type) {
        case ATADEV_PATA:
            pata_init(&devices[0]);
            break;
    }

    return 0;
}

dev_t atadev = {
    .probe = ata_probe,
    .read = ata_read,
    .write = ata_write,

    .f_ops = {
        .open = generic_file_open,
        .read = generic_file_read,
    },
};
