#ifndef _MBR_H
#define _MBR_H

#include <core/system.h>

struct mbr_part;
struct mbr;

#define MBR_TYPE_UNUSED     0x00
#define MBR_BOOT_SIGNATURE  0xAA55

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

struct mbr {
    /* bootloader code */
    uint8_t bootloader[440];
    uint32_t disk_signiture;
    uint16_t copy_protected;

    /* partition table */
    struct mbr_part ptab[4];
    uint16_t boot_signature;
} __packed;

#endif
