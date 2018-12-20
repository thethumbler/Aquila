#include <core/system.h>
#include <cpu/io.h>
#include <dev/dev.h>
#include <bits/errno.h>
#include <fdc.h>

struct dev fdcdev;

struct ioaddr fdcio;

#define SECTOR_SIZE  512UL

#define FDC_COMMAND_TIMEOUT 50

int fdc_wait()
{
    int need_reset = 1;
    for (int i = 0; i < FDC_COMMAND_TIMEOUT; ++i) {
        uint8_t msr;
        if ((msr = io_in8(&fdcio, FDC_MSR)) & FDC_MSR_RQM) {
            /* TODO Check DIO */
            need_reset = 0;
            break;
        }
    }

    return need_reset;
}

int fdc_cmd_send(uint8_t cmd, int argc, const uint8_t argv[], int resc, uint8_t resv[])
{
    uint8_t msr = io_in8(&fdcio, FDC_MSR);
    if (!(msr & FDC_MSR_RQM) || (msr & FDC_MSR_DIO)) {
        /* TODO */
        panic("fdc: reset controller\n");
    }

    /* Command Phase */
    io_out8(&fdcio, FDC_FIFO, cmd);

    for (int i = 0; i < argc; ++i) {
        if (fdc_wait())
            panic("fdc: need reset\n");

        msr = io_in8(&fdcio, FDC_MSR);
        printk("msr = %x\n", msr);

        io_out8(&fdcio, FDC_FIFO, argv[i]);
    }

    /* Execution Phase */
    msr = io_in8(&fdcio, FDC_MSR);

    if (msr & FDC_MSR_NDMA) {
        /* TODO Wait for execution phase */
    }

    /* Result Phase */
    //msr = io_in8(&fdcio, FDC_MSR);
    if (fdc_wait())
        panic("fdc: need reset\n");

    /* read result */
    for (int i = 0; i < resc; ++i) {
        if (fdc_wait())
            panic("fdc: need reset\n");

        resv[i] = io_in8(&fdcio, FDC_FIFO);
    }

    //msr = io_in8(&fdcio, FDC_MSR);
    //printk("msr = %x\n", msr);

    return 0;
}

int fdc_cmd_version()
{
    uint8_t version;
    fdc_cmd_send(FDC_CMD_VERSION, 0, 0, 1, &version);
    return version;
}

int fdc_reset()
{
    printk("fdc: Reset controller\n");

    //io_out8(&fdcio, FDC_DOR, FDC_DOR_MOTA);
    //io_out8(&fdcio, FDC_FIFO, FDC_CMD_VERSION);

    //for (int i = 0; i < 5; ++i) {
    //    uint8_t result = io_in8(&fdcio, FDC_FIFO);
    //    printk("result = %d\n", result);
    //}

    return 0;
}

int fdc_probe()
{
    fdcio.addr = FDC_BASE;
    fdcio.type = IOADDR_PORT;

    if (fdc_cmd_version() == 0x90) {
        printk("fdc: Initializing\n");
        fdc_reset();
    }

    kdev_blkdev_register(2, &fdcdev);
    return 0;
}

struct dev fdcdev = {
    .name  = "fdc",
    .probe = fdc_probe,
    //.read  = ata_read,
    //.write = ata_write,
    //.getbs = ata_getbs,

    //.fops = {
    //    .open  = posix_file_open,
    //    .read  = posix_file_read,
    //    .write = posix_file_write,
    //},
};

MODULE_INIT(fdc, fdc_probe, NULL)
