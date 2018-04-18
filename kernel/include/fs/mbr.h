#ifndef _MBR_H
#define _MBR_H

#include <core/system.h>
#include <fs/vfs.h>

#define MBR_TYPE_UNUSED             0x00
#define MBR_BOOT_SIGNATURE          0xAA55

typedef struct {
    uint8_t   status;
    struct {
        uint8_t   h;
        uint16_t  cs;
    } __packed start_chs;
    uint8_t   type;
    struct {
        uint8_t   h;
        uint16_t  cs;
    } __packed end_chs;
    uint32_t  start_lba;
    uint32_t  sectors_count;
} __packed partition_t;

typedef struct {
    uint8_t              bootloader[440];   // Actual Bootloader code
    uint32_t             disk_signiture;
    uint16_t             copy_protected;
    partition_t          ptab[4];           // Partition table
    uint16_t             boot_signature;
} __packed mbr_t;

void readmbr(struct inode *node);

#endif
