#ifndef _X86_SDT_H
#define _X86_SDT_H

#include <core/system.h>

struct acpi_rsdp {
    char     signature[8];
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  rev;
    uint32_t rsdt;
} __packed;

/* ACPI 2.0 (rev >= 2): the XSDT, 64-bit addresses */
struct acpi_rsdp2 {
    struct acpi_rsdp v1;
    uint32_t length;
    uint64_t xsdt;
    uint8_t  xchecksum;
    uint8_t  reserved[3];
} __packed;

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

struct acpi_rsdt {
    struct acpi_sdt_header header;
    uint32_t sdt[0];
} __packed;

struct acpi_xsdt {
    struct acpi_sdt_header header;
    uint64_t sdt[0];
} __packed;

/* an address of a register (Generic Address Structure) */
struct acpi_gas {
    uint8_t  space;         /* 0: memory, 1: I/O ports */
    uint8_t  bit_width;
    uint8_t  bit_offset;
    uint8_t  access_size;
    uint64_t address;
} __packed;

struct acpi_hpet {
    struct acpi_sdt_header header;
    uint32_t event_timer_block_id;
    struct acpi_gas address;
    uint8_t  hpet_number;
    uint16_t minimum_tick;
    uint8_t  page_protection;
} __packed;

#define ACPI_MADT_LOCAL_APIC    0
#define ACPI_MADT_IO_APIC       2
#define ACPI_MADT_INT_SRC_OVERRIDE      3

struct acpi_madt_entry {
    uint8_t type;
    uint8_t length;
    union {
        struct { /* LOCAL APIC */
            uint8_t processor_id;
            uint8_t apic_id;
            uint32_t flags;
        } __packed local_apic;
        struct { /* IO APIC */
            uint8_t ioapic_id;
            uint8_t __reserved;
            uint32_t ioapic_address;
            uint32_t global_system_interrupt_base;
        } __packed ioapic;
        struct { /* INTERRUPT SOURCE OVERRIDE */
            uint8_t bus_source;
            uint8_t irq_source;
            uint32_t global_system_interrupt;
        } __packed interrupt_source_override;
    } __packed data;
}__packed;

struct acpi_madt {
    struct acpi_sdt_header header;
    uint32_t local_controller_address;
    uint32_t flags;
    struct acpi_madt_entry entry[0];
}__packed;

#endif /* ! _X86_SDT_H */
