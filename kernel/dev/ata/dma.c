/*
 * ATA bus master DMA (IDE controllers, as QEMU's PIIX): the controller
 * moves the data of a command to or from memory itself, and the request
 * waits for the drive's interrupt. PIO moves it a word at a time through
 * an I/O port and polls, which under a hypervisor exits to it every time.
 *
 * Transfers go through a buffer of the channel's, physically contiguous
 * and within 64 KiB as the controller needs, described by one PRD entry.
 */

#include <core/system.h>
#include <core/string.h>
#include <cpu/io.h>
#include <mm/mm.h>
#include <sys/sched.h>
#include <aquila/errno.h>

#include <ata.h>

/* bus master registers */
#define BM_CMD          0
#define BM_STATUS       2
#define BM_PRDT         4

#define BM_CMD_START    0x01
#define BM_CMD_READ     0x08    /* from the drive to memory */

#define BM_ST_ACTIVE    0x01
#define BM_ST_ERROR     0x02
#define BM_ST_IRQ       0x04

#define DMA_BUF_SIZE    (64 * 1024)
#define DMA_MAX_SECTORS (DMA_BUF_SIZE / 512)

/* physical region descriptor */
struct prd {
    uint32_t addr;
    uint16_t size;      /* 0 is 64 KiB */
    uint16_t flags;
} __packed;

#define PRD_EOT         0x8000

/* per channel, in the kernel image: its physical address is LMA() */
static struct prd prdt[2] __aligned(16);
static char dma_buf[2][DMA_BUF_SIZE] __aligned(DMA_BUF_SIZE);

static int channel_index(struct ata_drive *drive)
{
    return drive->id / 2;
}

/* the transfer is over: what the controller and the drive say, which
 * also acknowledges the interrupt */
static void dma_complete(struct ata_channel *ch, uint8_t bm_status)
{
    ch->status = io_in8(&ch->base, ATA_REG_STATUS);
    ch->bm_status = bm_status;
    io_out8(&ch->bm, BM_STATUS, BM_ST_IRQ | BM_ST_ERROR);
    ch->done = 1;
}

/* the channel's interrupt */
void dma_irq(struct ata_channel *ch)
{
    uint8_t bm_status = ch->bm.addr ? io_in8(&ch->bm, BM_STATUS) : 0;

    /* not from a DMA transfer (PIO polls), the status read acknowledges it */
    if (!(bm_status & BM_ST_IRQ)) {
        io_in8(&ch->base, ATA_REG_STATUS);
        return;
    }

    dma_complete(ch, bm_status);
    thread_queue_wakeup_nopoll(&ch->irq_wait);
}

static int dma_transfer(struct ata_drive *drive, uint64_t lba, size_t count, int write)
{
    struct ata_channel *ch = drive->channel;
    int c = channel_index(drive);

    prdt[c].addr  = (uint32_t) (uintptr_t) LMA(&dma_buf[c][0]);
    prdt[c].size  = (count * 512) & 0xFFFF;
    prdt[c].flags = PRD_EOT;

    /* stopped, the direction, the table, interrupt and error cleared */
    io_out8(&ch->bm, BM_CMD, write ? 0 : BM_CMD_READ);
    io_out32(&ch->bm, BM_PRDT, (uint32_t) (uintptr_t) LMA(&prdt[c]));
    io_out8(&ch->bm, BM_STATUS, BM_ST_IRQ | BM_ST_ERROR);

    ata_setup(drive, lba, count);
    ch->done = 0;

    if (drive->mode == ATA_MODE_LBA48)
        io_out8(&drive->base, ATA_REG_CMD, write ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT);
    else
        io_out8(&drive->base, ATA_REG_CMD, write ? ATA_CMD_WRITE_DMA : ATA_CMD_READ_DMA);

    io_out8(&ch->bm, BM_CMD, (write ? 0 : BM_CMD_READ) | BM_CMD_START);

    /* a process sleeps until the interrupt, before there are any (mounting
     * the root, loading init at boot) the status is polled */
    while (!ch->done) {
        if (sched_running && curthread) {
            thread_queue_sleep(&ch->irq_wait);
        } else {
            uint8_t bm_status = io_in8(&ch->bm, BM_STATUS);

            if (bm_status & BM_ST_IRQ)
                dma_complete(ch, bm_status);
        }
    }

    io_out8(&ch->bm, BM_CMD, 0);

    if ((ch->bm_status & BM_ST_ERROR) || (ch->status & (ATA_STATUS_ERR | ATA_STATUS_DF))) {
        printk("ata%d: DMA %s error, status 0x%x, bus master 0x%x\n", drive->id,
                write ? "write" : "read", ch->status, ch->bm_status);
        return -EIO;
    }

    return 0;
}

static ssize_t dma_read(struct ata_drive *drive, uint64_t lba, size_t count, void *buf)
{
    char *cbuf = buf;

    while (count) {
        size_t n = MIN(count, DMA_MAX_SECTORS);
        int err;

        if ((err = dma_transfer(drive, lba, n, 0)))
            return err;

        memcpy(cbuf, dma_buf[channel_index(drive)], n * 512);

        lba   += n;
        count -= n;
        cbuf  += n * 512;
    }

    return 0;
}

static ssize_t dma_write(struct ata_drive *drive, uint64_t lba, size_t count, void *buf)
{
    char *cbuf = buf;

    while (count) {
        size_t n = MIN(count, DMA_MAX_SECTORS);
        int err;

        memcpy(dma_buf[channel_index(drive)], cbuf, n * 512);

        if ((err = dma_transfer(drive, lba, n, 1)))
            return err;

        lba   += n;
        count -= n;
        cbuf  += n * 512;
    }

    return 0;
}

/* DMA for a drive that can, if its controller can; 0 if it does now */
int dma_init(struct ata_drive *drive)
{
    if (!drive->channel->bm.addr || !(drive->capabilities & ATA_CAP_DMA))
        return -1;

    drive->read  = dma_read;
    drive->write = dma_write;
    return 0;
}
