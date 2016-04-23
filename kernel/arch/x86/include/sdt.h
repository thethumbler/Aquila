#ifndef _X86_SDT_H
#define _X86_SDT_H

struct acpi_rsdp
{
	char signature[8];
	uint8_t checksum;
	char oem_id[6];
	uint8_t rev;
	uint32_t rsdt;
} __attribute__((packed));

struct acpi_sdt_header
{
  char signature[4];
  uint32_t length;
  uint8_t rev;
  uint8_t checksum;
  char oemid[6];
  char oem_table_id[8];
  uint32_t oem_rev;
  uint32_t creator_id;
  uint32_t creator_rev;
};

struct acpi_rsdt
{
	struct acpi_sdt_header header;
	uint32_t sdt[0];
};

#define ACPI_MADT_LOCAL_APIC	0
#define ACPI_MADT_IO_APIC		2
#define ACPI_MADT_INT_SRC_OVERRIDE		3

struct acpi_madt_entry
{
	uint8_t type;
	uint8_t length;
	union
	{
		struct /* LOCAL APIC */
		{
			uint8_t processor_id;
			uint8_t apic_id;
			uint32_t flags;
		}__attribute__((packed)) local_apic;
		struct /* IO APIC */
		{
			uint8_t ioapic_id;
			uint8_t __reserved;
			uint32_t ioapic_address;
			uint32_t global_system_interrupt_base;
		}__attribute__((packed)) ioapic;
		struct /* INTERRUPT SOURCE OVERRIDE */
		{
			uint8_t bus_source;
			uint8_t irq_source;
			uint32_t global_system_interrupt;
		}__attribute__((packed)) interrupt_source_override;
	} __attribute__((packed)) data;
}__attribute__((packed));

struct acpi_madt
{
	struct acpi_sdt_header header;
	uint32_t local_controller_address;
	uint32_t flags;
	struct acpi_madt_entry entry[0];
}__attribute__((packed));

#endif