#ifndef _FS_MBR_H
#define _FS_MBR_H

#include <core/system.h>

struct mbr_part;
struct mbr;

/* The master boot record: a disk's first sector, with its partition
 * table (fs/mbr.c reads it). */

/* A partition entry's type for an unused entry; the MBR's signature. */
#define MBR_TYPE_UNUSED     0x00
#define MBR_BOOT_SIGNATURE  0xAA55

/** A partition table entry. */
struct mbr_part {
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
} __packed;

/** The master boot record. */
struct mbr {
    /* the boot loader's code */
    uint8_t bootloader[440];
    uint32_t disk_signiture;
    uint16_t copy_protected;

    /* the partition table */
    struct mbr_part ptab[4];
    uint16_t boot_signature;
} __packed;

#endif /* ! _FS_MBR_H */
