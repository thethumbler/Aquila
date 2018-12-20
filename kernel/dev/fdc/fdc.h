#ifndef _FDC_H
#define _FDC_H

#define FDC_BASE 0x3F0

#define FDC_SRA      0   /* Status Register A */
#define FDC_SRB      1   /* Status Register B  */
#define FDC_DOR      2   /* Digital Output Register */
#define FDC_TDR      3   /* Tape Drive Register */
#define FDC_MSR      4   /* Main Status Register */
#define FDC_DSR      4   /* Datarate Select Register */
#define FDC_FIFO     5   /* Data FIFO */
#define FDC_DIR      7   /* Digital Input Register */
#define FDC_CCR      7   /* Configuration Control Register */


#define FDC_DOR_MOTD    0x80
#define FDC_DOR_MOTC    0x40
#define FDC_DOR_MOTB    0x20
#define FDC_DOR_MOTA    0x10
#define FDC_DOR_IRQ     0x08
#define FDC_DOR_RESET   0x04
#define FDC_DOR_DSEL    0x03

#define FDC_MSR_RQM     0x80
#define FDC_MSR_DIO     0x40
#define FDC_MSR_NDMA    0x20
#define FDC_MSR_CB      0x10
#define FDC_MSR_ACTD    0x08
#define FDC_MSR_ACTC    0x04
#define FDC_MSR_ACTB    0x02
#define FDC_MSR_ACTA    0x01

#define FDC_CMD_READ_TRACK          2
#define FDC_CMD_SPECIFY             3
#define FDC_CMD_SENSE_DRIVE_STATUS  4
#define FDC_CMD_WRITE_DATA          5
#define FDC_CMD_READ_DATA           6
#define FDC_CMD_RECALIBRATE         7
#define FDC_CMD_SENSE_INTERRUPT     8
#define FDC_CMD_WRITE_DELETED_DATA  9
#define FDC_CMD_READ_ID             10
#define FDC_CMD_READ_DELETED_DATA   12
#define FDC_CMD_FORMAT_TRACK        13
#define FDC_CMD_DUMPREG             14
#define FDC_CMD_SEEK                15
#define FDC_CMD_VERSION             16
#define FDC_CMD_SCAN_EQUAL          17
#define FDC_CMD_PERPENDICULAR_MODE  18
#define FDC_CMD_CONFIGURE           19
#define FDC_CMD_LOCK                20
#define FDC_CMD_VERIFY              22
#define FDC_CMD_SCAN_LOW_OR_EQUAL   25
#define FDC_CMD_SCAN_HIGH_OR_EQUAL  29

#endif /* ! _FDC_H */
