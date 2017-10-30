#ifndef _IDE_H
#define _IDE_H

#define ATADEV_UNKOWN   0
#define ATADEV_PATA     1
#define ATADEV_SATA     2
#define ATADEV_PATAPI   3
#define ATADEV_SATAPI   4

#define ATA_PORT_DATA   0x00   /* Data Port */
#define ATA_PORT_ERROR  0x01   /* Features / Error Information */
#define ATA_PORT_COUNT  0x02   /* Sector Count	*/
#define ATA_PORT_LBALO  0x03   /* Sector Number / LBA */
#define ATA_PORT_LBAMI  0x04   /* Cylinder Low / LBA */
#define ATA_PORT_LBAHI  0x05   /* Cylinder High / LBA */
#define ATA_PORT_DRIVE  0x06   /* Drive / Head Port */
#define ATA_PORT_CMD    0x07   /* Command port / Regular Status port */

#define ATA_PIO_PORT_BASE       0x1F0
#define ATA_PIO_PORT_CONTROL    0x3F6

#define ATA_CMD_RESET   0x40
#define ATA_CMD_READ_SECOTRS_EXT    0x24
#define ATA_DRIVE_LBA48 0x40

#define ATA_STATUS_BSY     0x80    // Busy
#define ATA_STATUS_DRDY    0x40    // Drive ready
#define ATA_STATUS_DF      0x20    // Drive write fault
#define ATA_STATUS_DSC     0x10    // Drive seek complete
#define ATA_STATUS_DRQ     0x08    // Data request ready
#define ATA_STATUS_CORR    0x04    // Corrected data
#define ATA_STATUS_IDX     0x02    // Inlex
#define ATA_STATUS_ERR     0x01    // Error

union status_reg {
    struct {
        unsigned bsy : 1;
        unsigned rdy : 1;
        unsigned df  : 1;
        unsigned srv : 1;
        unsigned drq : 1;
        unsigned err : 1;
    } __packed bits;
    uint8_t raw;
} __packed;

#endif
