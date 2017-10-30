#include <core/system.h>
#include <core/string.h>

#include <cpu/cpu.h>

static inline int byte_checksum(const char *p, size_t s)
{
	int8_t ret = 0;
	while (s--) ret += (int8_t) *p++;
	return ret;
}

struct acpi_rsdp *_rsdp = NULL;
struct acpi_rsdp *get_acpi_rsdp()
{
	if (_rsdp) return _rsdp;

	char *EBDA = (char *) (uintptr_t) *(uint16_t *) (0x40E);
	
	char *p = EBDA;

	while (p <= EBDA + 0x400) {
		if (!strncmp(p, "RSD PTR ", 8))
			if (!byte_checksum(p, sizeof(struct acpi_rsdp)))
				return _rsdp = (struct acpi_rsdp *) p;

		p += 0x10;
	}

	while (p < VMA((char *) 0x000FFFFF)) {
		if (!strncmp(p, "RSD PTR ", 8))
			if (!byte_checksum(p, sizeof(struct acpi_rsdp)))
				return _rsdp = (struct acpi_rsdp *) p;
		
		p += 0x10;
	}

	return NULL;
}

struct acpi_rsdt *_rsdt = NULL;
struct acpi_rsdt *get_acpi_rsdt()
{
	if (_rsdt) return _rsdt;

	struct acpi_rsdp *rsdp = get_acpi_rsdp();
	if (!rsdp) return NULL;

	return _rsdt = (struct acpi_rsdt *) rsdp->rsdt;
}

struct acpi_madt *_madt = NULL;
struct acpi_madt *get_acpi_madt()
{
	if (_madt) return _madt;

	struct acpi_rsdt *rsdt = get_acpi_rsdt();
	if (!rsdt) return NULL;

	int entries = (rsdt->header.length - sizeof(rsdt->header))/sizeof(uint32_t);
	
	int i;	
	for (i = 0; i < entries; ++i)
		if (!strncmp(((struct acpi_sdt_header*) rsdt->sdt[i])->signature, "APIC", 4))
			if (!byte_checksum((char*) rsdt->sdt[i], ((struct acpi_sdt_header*) rsdt->sdt[i])->length))
				return _madt = (struct acpi_madt *) rsdt->sdt[i];

	return NULL;
}

int get_cpus_count()
{
	int ret = 0;

	struct acpi_madt *madt = get_acpi_madt();
	struct acpi_madt_entry *entry = madt->entry;

	while ((uint32_t) entry < ((uint32_t) madt + madt->header.length)) {
		if (entry->type == ACPI_MADT_LOCAL_APIC) {
			//printk("CPU %d:  LAPIC_ID: %d FLAGS: %d\n",
			//	entry->data.local_apic.processor_id,
			//	entry->data.local_apic.apic_id,
			//	entry->data.local_apic.flags
			//);

			++ret;
		}

		entry = (struct acpi_madt_entry *)((uint32_t) entry + entry->length);
	}

	return ret;
}
