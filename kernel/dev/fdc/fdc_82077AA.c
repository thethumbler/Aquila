#include <core/system.h>
#include <core/panic.h>
#include <core/module.h>
#include <fs/posix.h>

#include <cpu/io.h>
#include <dev/dev.h>
#include <bits/errno.h>
#include <fdc_82077AA.h>

static struct dev fdcdev;
static struct ioaddr fdcio;

#define SECTOR_SIZE  512UL
#define FDC_COMMAND_TIMEOUT 500000

static struct fdc_drive {
    uint8_t dor;
} drives[4];

static int fdc_wait(int check_dio)
{
    int need_reset = 1;
    check_dio = check_dio? FDC_MSR_DIO : 0;

    for (int i = 0; i < FDC_COMMAND_TIMEOUT; ++i) {
        uint8_t msr = io_in8(&fdcio, FDC_MSR);
        if ((msr & (FDC_MSR_RQM | check_dio)) == FDC_MSR_RQM) {
            need_reset = 0;
            break;
        }
    }

    return need_reset;
}

static int fdc_cmd_send(uint8_t cmd, int argc, const uint8_t argv[], int resc, uint8_t resv[])
{
    if (fdc_wait(1)) {
        /* TODO */
        panic("fdc: reset controller\n");
    }

    uint8_t msr;

    /* Command Phase */
    io_out8(&fdcio, FDC_FIFO, cmd);

    for (int i = 0; i < argc; ++i) {
        if (fdc_wait(1))
            panic("fdc: need reset\n");

        msr = io_in8(&fdcio, FDC_MSR);
        io_out8(&fdcio, FDC_FIFO, argv[i]);
    }

    /* Execution Phase */
    int count = 0;

__next:
    msr = io_in8(&fdcio, FDC_MSR);
    while ((msr & (FDC_MSR_RQM | FDC_MSR_NDMA)) == (FDC_MSR_RQM | FDC_MSR_NDMA)) {
        ++count;
        uint8_t byte = io_in8(&fdcio, FDC_FIFO);
        msr = io_in8(&fdcio, FDC_MSR);
    }

    if (msr & FDC_MSR_NDMA)
        goto __next;

    /* Result Phase */
    //msr = io_in8(&fdcio, FDC_MSR);
    if (fdc_wait(0))
        panic("fdc: need reset\n");

    /* read result */
    for (int i = 0; i < resc; ++i) {
        if (fdc_wait(0))
            panic("fdc: need reset\n");

        resv[i] = io_in8(&fdcio, FDC_FIFO);
    }

    msr = io_in8(&fdcio, FDC_MSR);
    return 0;
}

static int fdc_cmd_version()
{
    uint8_t version;
    fdc_cmd_send(FDC_CMD_VERSION, 0, 0, 1, &version);

    return version;
}

static int fdc_cmd_sense()
{
    uint8_t resv[2];
    fdc_cmd_send(FDC_CMD_SENSE_INTERRUPT, 0, 0, 2, resv);

    //printk("resv[0] = %x, resv[1] = %x\n", resv[0], resv[1]);
    return 0;
}

static int fdc_reset()
{
    printk("fdc: Reset controller\n");

    uint8_t dor = io_in8(&fdcio, FDC_DOR);
    //printk("dor = %x\n", dor);

    io_out8(&fdcio, FDC_DOR, 0);
    io_out8(&fdcio, FDC_DOR, dor | FDC_DOR_MOTA);

    for (int i = 0; i < 4; ++i)
        fdc_cmd_sense();

    return 0;
}

static int fdc_configure()
{
    uint8_t params[] = {0, FDC_CONF_SEEK_EN | 8, 0};
    fdc_cmd_send(FDC_CMD_CONFIGURE, 3, params, 0, 0);
    return 0;
}

static int fdc_lock()
{
    uint8_t lock;
    fdc_cmd_send(FDC_CMD_LOCK | FDC_CMD_MT, 0, 0, 1, &lock);
    return 0;
}

static int fdc_recalibrate()
{
    uint8_t drive = 0;
    fdc_cmd_send(FDC_CMD_RECALIBRATE, 1, &drive, 0, 0);
    return 0;
}

#define FLOPPY_144_SECTORS_PER_TRACK    18
static int fdc_sect_rw(int wr, size_t lba, void *_buf)
{
    uint8_t *buf  = (uint8_t *) _buf;
    uint8_t drive = 0;
    uint8_t cyln  = lba / (2 * FLOPPY_144_SECTORS_PER_TRACK);
    uint8_t head  = ((lba % (2 * FLOPPY_144_SECTORS_PER_TRACK)) / FLOPPY_144_SECTORS_PER_TRACK);
    uint8_t sect  = ((lba % (2 * FLOPPY_144_SECTORS_PER_TRACK)) % FLOPPY_144_SECTORS_PER_TRACK + 1);

    uint8_t cmd = (wr? FDC_CMD_WRITE_DATA : FDC_CMD_READ_DATA) | FDC_CMD_MF;
    uint8_t argv[8], resv[7];
    int argc = 8, resc = 7;

    argv[0] = (head << 2) | drive;
    argv[1] = cyln;
    argv[2] = head;
    argv[3] = sect;
    argv[4] = 2;
    argv[5] = sect>=FLOPPY_144_SECTORS_PER_TRACK?FLOPPY_144_SECTORS_PER_TRACK:sect;
    argv[6] = 0x1B;
    argv[7] = 0xFF;

    if (fdc_wait(1)) {
        panic("fdc: reset controller\n");
    }

    uint8_t msr;

    /* Command Phase */
    io_out8(&fdcio, FDC_FIFO, cmd);

    for (int i = 0; i < argc; ++i) {
        if (fdc_wait(1))
            panic("fdc: need reset\n");

        msr = io_in8(&fdcio, FDC_MSR);
        io_out8(&fdcio, FDC_FIFO, argv[i]);
    }

    /* Execution Phase */
    int count = 0;

__next:
    msr = io_in8(&fdcio, FDC_MSR);
    while ((msr & (FDC_MSR_RQM | FDC_MSR_NDMA)) == (FDC_MSR_RQM | FDC_MSR_NDMA)) {
        if (wr) {
            io_out8(&fdcio, FDC_FIFO, buf[count]);
            ++count;
        } else {
            buf[count] = io_in8(&fdcio, FDC_FIFO);
            ++count;
        }

        msr = io_in8(&fdcio, FDC_MSR);
    }

    if (msr & FDC_MSR_NDMA)
        goto __next;

    /* Result Phase */
    if (fdc_wait(0))
        panic("fdc: need reset\n");

    /* read result */
    for (int i = 0; i < resc; ++i) {
        if (fdc_wait(0))
            panic("fdc: need reset\n");

        resv[i] = io_in8(&fdcio, FDC_FIFO);
    }

    msr = io_in8(&fdcio, FDC_MSR);
    return 0;
}

static int fdc_probe()
{
    fdcio.addr = FDC_BASE;
    fdcio.type = IOADDR_PORT;

    fdc_reset();

    if (fdc_cmd_version() != 0x90) {
        printk("fdc: Controller not found\n");
        return -1;
    }

    printk("fdc: Initializing\n");

    fdc_reset();
    fdc_configure();
    fdc_lock();
    fdc_reset();
    fdc_recalibrate();

    kdev_blkdev_register(2, &fdcdev);

    return 0;
}

static size_t fdc_getbs(struct devid *dd __unused)
{
    return 512;
}

static ssize_t fdc_read(struct devid *dd, off_t offset, size_t size, void *buf)
{
    //printk("fdc_read(dd=%p, offset=%d, size=%d, buf=%p)\n", dd, offset, size, buf);
    fdc_sect_rw(0, offset, buf);
    return 512;
}

static struct dev fdcdev = {
    .name  = "fdc_82077AA",
    .probe = fdc_probe,
    .read  = fdc_read,
    .getbs = fdc_getbs,

    .fops = {
        .open  = posix_file_open,
        .read  = posix_file_read,
    },
};

MODULE_INIT(fdc_82077AA, fdc_probe, NULL)
