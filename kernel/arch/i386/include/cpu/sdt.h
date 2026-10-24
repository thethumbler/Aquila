#ifndef _X86_SDT_H
#define _X86_SDT_H

#include <core/system.h>

/* x86: ACPI's tables, as the firmware lays them out (see the ACPI
 * specification). acpi.c finds them, apic.c and hpet.c read them. */

/** The Root System Description Pointer, which says where the RSDT is. */
struct acpi_rsdp {
    char     signature[8];
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  rev;
    uint32_t rsdt;
} __packed;

/** ACPI 2.0's RSDP (revision 2 or more), which also says where the XSDT
 * is, with 64-bit addresses. */
struct acpi_rsdp2 {
    struct acpi_rsdp v1;
    uint32_t length;
    uint64_t xsdt;
    uint8_t  xchecksum;
    uint8_t  reserved[3];
} __packed;

/** The header that every table starts with. */
struct acpi_sdt_header {
    char     signature[4];
    uint32_t length;
    uint8_t  rev;
    uint8_t  checksum;
    char     oemid[6];
    char     oem_table_id[8];
    uint32_t oem_rev;
    uint32_t creator_id;
    uint32_t creator_rev;
} __packed;

/** The RSDT: the other tables' 32-bit addresses. */
struct acpi_rsdt {
    struct acpi_sdt_header header;
    uint32_t sdt[0];
} __packed;

/** The XSDT: the other tables' 64-bit addresses. */
struct acpi_xsdt {
    struct acpi_sdt_header header;
    uint64_t sdt[0];
} __packed;

/** A register's address, ACPI's Generic Address Structure. */
struct acpi_gas {
    uint8_t  space;         /* 0: memory, 1: I/O ports */
    uint8_t  bit_width;
    uint8_t  bit_offset;
    uint8_t  access_size;
    uint64_t address;
} __packed;

/** The HPET table: where the HPET's registers are. */
struct acpi_hpet {
    struct acpi_sdt_header header;
    uint32_t event_timer_block_id;
    struct acpi_gas address;
    uint8_t  hpet_number;
    uint16_t minimum_tick;
    uint8_t  page_protection;
} __packed;

/* The MADT (signature "APIC") lists the interrupt controllers, in
 * entries after its header, each with a type and a length. */
#define ACPI_MADT_LOCAL_APIC        0
#define ACPI_MADT_IO_APIC           1
#define ACPI_MADT_INT_SRC_OVERRIDE  2
#define ACPI_MADT_LOCAL_APIC_NMI    4

#define ACPI_MADT_PCAT_COMPAT       0x1     /* the machine has 8259s too */

#define ACPI_MADT_ENABLED           0x1     /* a local APIC's flag: its CPU is there */

/* an override's flags: the polarity (bits 0 and 1), the trigger mode
 * (bits 2 and 3) */
#define ACPI_MPS_POLARITY_MASK      0x3
#define ACPI_MPS_POLARITY_HIGH      0x1
#define ACPI_MPS_POLARITY_LOW       0x3
#define ACPI_MPS_TRIGGER_MASK       0xc
#define ACPI_MPS_TRIGGER_EDGE       0x4
#define ACPI_MPS_TRIGGER_LEVEL      0xc

/** The start of every MADT entry. */
struct acpi_madt_entry {
    uint8_t type;
    uint8_t length;
} __packed;

/** A CPU's local APIC. */
struct acpi_madt_local_apic {
    struct acpi_madt_entry entry;
    uint8_t  processor_id;
    uint8_t  apic_id;
    uint32_t flags;             /* bit 0: enabled */
} __packed;

/** An I/O APIC: its registers, and the first GSI its pins bring. */
struct acpi_madt_io_apic {
    struct acpi_madt_entry entry;
    uint8_t  id;
    uint8_t  reserved;
    uint32_t address;
    uint32_t gsi_base;
} __packed;

/** An ISA IRQ that comes on another GSI, or with another polarity or
 * trigger mode, than the default. */
struct acpi_madt_override {
    struct acpi_madt_entry entry;
    uint8_t  bus;               /* 0: ISA */
    uint8_t  source;            /* the ISA IRQ */
    uint32_t gsi;
    uint16_t flags;
} __packed;

/** Which local APIC input (LINT0 or LINT1) is the NMI. */
struct acpi_madt_local_apic_nmi {
    struct acpi_madt_entry entry;
    uint8_t  processor_id;      /* 0xff for all CPUs */
    uint16_t flags;
    uint8_t  lint;              /* LINT0 or LINT1 */
} __packed;

/** The MADT's header: the local APICs' address, and flags. */
struct acpi_madt {
    struct acpi_sdt_header header;
    uint32_t local_apic_address;
    uint32_t flags;
} __packed;

#endif /* ! _X86_SDT_H */
