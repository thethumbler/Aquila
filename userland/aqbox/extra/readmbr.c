#include <aqbox.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>

#define MBR_TYPE_UNUSED             0x00
#define MBR_BOOT_SIGNATURE          0xAA55

const char *part_type[] = {
    [0x00] = "Empty",
    [0x01] = "FAT12",
    [0x02] = "XENIX root",
    [0x03] = "XENIX usr",
    [0x04] = "FAT16 <32M",
    [0x05] = "Extended",
    [0x06] = "FAT16",
    [0x07] = "HPFS/NTFS/exFAT",
    [0x08] = "AIX",
    [0x09] = "AIX bootable",
    [0x0a] = "OS/2 Boot Manag",
    [0x0b] = "W95 FAT32",
    [0x0c] = "W95 FAT32 (LBA)",
    [0x0e] = "W95 FAT16 (LBA)",
    [0x0f] = "W95 Ext'd (LBA)",
    [0x10] = "OPUS",
    [0x11] = "Hidden FAT12",
    [0x12] = "Compaq diagnost",
    [0x14] = "Hidden FAT16 <3",
    [0x16] = "Hidden FAT16",
    [0x17] = "Hidden HPFS/NTF",
    [0x18] = "AST SmartSleep",
    [0x1b] = "Hidden W95 FAT3",
    [0x1c] = "Hidden W95 FAT3",
    [0x1e] = "Hidden W95 FAT1",
    [0x24] = "NEC DOS",
    [0x27] = "Hidden NTFS Win",
    [0x39] = "Plan 9",
    [0x3c] = "PartitionMagic",
    [0x40] = "Venix 80286",
    [0x41] = "PPC PReP Boot",
    [0x42] = "SFS",
    [0x4d] = "QNX4.x",
    [0x4e] = "QNX4.x 2nd part",
    [0x4f] = "QNX4.x 3rd part",
    [0x50] = "OnTrack DM",
    [0x51] = "OnTrack DM6 Aux",
    [0x52] = "CP/M",
    [0x53] = "OnTrack DM6 Aux",
    [0x54] = "OnTrackDM6",
    [0x55] = "EZ-Drive",
    [0x56] = "Golden Bow",
    [0x5c] = "Priam Edisk",
    [0x61] = "SpeedStor",
    [0x63] = "GNU HURD or Sys",
    [0x64] = "Novell Netware",
    [0x65] = "Novell Netware ",
    [0x70] = "DiskSecure Mult",
    [0x75] = "PC/IX ",
    [0x80] = "Old Minix",
    [0x81] = "Minix",
    [0x82] = "Linux swap / So",
    [0x83] = "Linux",
    [0x84] = "OS/2 hidden C: ",
    [0x85] = "Linux extended",
    [0x86] = "NTFS volume set",
    [0x87] = "NTFS volume set",
    [0x88] = "Linux plaintext",
    [0x8e] = "Linux LVM",
    [0x93] = "Amoeba",
    [0x94] = "Amoeba BBT",
    [0x9f] = "BSD/OS",
    [0xa0] = "IBM Thinkpad hi",
    [0xa5] = "FreeBSD",
    [0xa6] = "OpenBSD",
    [0xa7] = "NeXTSTEP",
    [0xa8] = "Darwin UFS",
    [0xa9] = "NetBSD",
    [0xab] = "Darwin boot",
    [0xaf] = "HFS / HFS+",
    [0xb7] = "BSDI fs",
    [0xb8] = "BSDI swap",
    [0xbb] = "Boot Wizard hid",
    [0xbe] = "Solaris boot",
    [0xbf] = "Solaris",
    [0xc1] = "DRDOS/sec (FAT-",
    [0xc4] = "DRDOS/sec (FAT-",
    [0xc6] = "DRDOS/sec (FAT-",
    [0xc7] = "Syrinx",
    [0xda] = "Non-FS data",
    [0xdb] = "CP/M / CTOS / .",
    [0xde] = "Dell Utility",
    [0xdf] = "BootIt",
    [0xe1] = "DOS access",
    [0xe3] = "DOS R/O",
    [0xe4] = "SpeedStor",
    [0xeb] = "BeOS fs",
    [0xee] = "GPT",
    [0xef] = "EFI (FAT-12/16/",
    [0xf0] = "Linux/PA-RISC b",
    [0xf1] = "SpeedStor",
    [0xf2] = "DOS secondary",
    [0xf4] = "SpeedStor",
    [0xfb] = "VMware VMFS",
    [0xfc] = "VMware VMKCORE",
    [0xfd] = "Linux raid auto",
    [0xfe] = "LANstep",
    [0xff] = "BBT",
};

struct part {
    uint8_t   status;
    struct {
        uint8_t  h;
        uint8_t  s: 6;
        uint16_t c: 10;
    } __packed start_chs;
    uint8_t   type;
    struct {
        uint8_t  h;
        uint8_t  s: 6;
        uint16_t c: 10;
    } __packed end_chs;
    uint32_t  start_lba;
    uint32_t  sectors_count;
} __packed;

struct mbr {
    /* Actual Bootloader code */
    uint8_t  bootloader[440];
    uint32_t disk_signiture;
    uint16_t copy_protected;

    /* Partition table */
    struct part ptab[4];

    uint16_t boot_signature;
} __packed;

static int readmbr(const char *path)
{
    int fd;
    
    if ((fd = open(path, O_RDONLY)) < 0) {
        perror("read");
        return -1;
    }

    struct mbr mbr;
    memset(&mbr, 0, sizeof(struct mbr));

    ssize_t r;
    if ((r = read(fd, &mbr, sizeof(struct mbr))) < 0) {
        close(fd);
        perror("read");
        return -1;
    }

    for (int i = 0; i < 4; ++i) {
        if (mbr.ptab[i].type == MBR_TYPE_UNUSED)
            continue;

        printf("Partition %d\n", i);
        printf("  Status: %x\n", mbr.ptab[i].status);
        unsigned type = mbr.ptab[i].type;
        printf("  Type: 0x%x (%s)\n", mbr.ptab[i].type, part_type[type]? part_type[type] : "Unkown");
        printf("  Start C/H/S: %u/%u/%u\n", mbr.ptab[i].start_chs.c, mbr.ptab[i].start_chs.h, mbr.ptab[i].start_chs.s);
        printf("  End C/H/S: %u/%u/%u\n", mbr.ptab[i].end_chs.c, mbr.ptab[i].end_chs.h, mbr.ptab[i].end_chs.s);
        printf("  Start LBA: 0x%lx\n", mbr.ptab[i].start_lba);
        printf("  Sectors: %lu\n", mbr.ptab[i].sectors_count);
    }

    return 0;
}

AQBOX_APPLET(readmbr)(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: readmbr DEVICE\n");
        return -1;
    }

    return readmbr(argv[1]);
}
