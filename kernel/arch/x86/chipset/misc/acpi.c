#include <core/system.h>
#include <core/string.h>

#include <cpu/cpu.h>

static inline int byte_checksum(const char *p, size_t s)
{
    int8_t ret = 0;
    while (s--) ret += (int8_t) *p++;
    return ret;
}

struct acpi_rsdp *acpi_rsdp_detect()
{
    uint16_t ebda_p = * VMA((uint16_t *) 0x40E);
    char *ebda = VMA((char *) (uintptr_t) ebda_p);
    
    char *p = ebda;

    while (p <= ebda + 0x400) {
        if (!strncmp(p, "RSD PTR ", 8))
            if (!byte_checksum(p, sizeof(struct acpi_rsdp)))
                return (struct acpi_rsdp *) p;

        p += 0x10;
    }

    while (p < VMA((char *) 0x000FFFFF)) {
        if (!strncmp(p, "RSD PTR ", 8))
            if (!byte_checksum(p, sizeof(struct acpi_rsdp)))
                return (struct acpi_rsdp *) p;
        
        p += 0x10;
    }

    return NULL;
}

/* Dummy page used for mapping ACPI SDT -- XXX */
static char __acpi_data[PAGE_SIZE] __aligned(PAGE_SIZE);
static uintptr_t __acpi_rsdt = 0;

#define __ACPI_ADDR(phy) ((uintptr_t) __acpi_data + (((uintptr_t)(phy)) & PAGE_MASK))

void acpi_setup()
{
    printk("ACPI: Detecting...\n");
    struct acpi_rsdp *rsdp = acpi_rsdp_detect();

    if (rsdp) {
        printk("ACPI: RSDP found at %p\n", rsdp);
    } else {
        printk("ACPI: RSDP not found\n");
        return;
    }

    /* Map to VM */
    uintptr_t rsdt_phy  = rsdp->rsdt;
    uintptr_t rsdt_page = rsdp->rsdt & ~PAGE_MASK;
    uintptr_t rsdt_vir  = __ACPI_ADDR(rsdt_phy);

    printk("ACPI: RSDT is located at %p\n", rsdt_phy);
    printk("ACPI: Remapping RSDT to %p\n", rsdt_vir);

    pmman.map_to(rsdt_page, (uintptr_t) &__acpi_data, PAGE_SIZE, KRW);
    __acpi_rsdt = rsdt_vir;
}

uintptr_t acpi_rsdt_find(char signature[4])
{
    struct acpi_rsdt *rsdt = (struct acpi_rsdt *) __acpi_rsdt;

    /* ACPI not initalized */
    if (!rsdt)
        return 0;

    int entries = (rsdt->header.length - sizeof(rsdt->header))/sizeof(uint32_t);

    for (int i = 0; i < entries; ++i) {
        struct acpi_sdt_header *hdr = (struct acpi_sdt_header *) __ACPI_ADDR(rsdt->sdt[i]);

        if (!strncmp(hdr->signature, signature, 4))
            return (uintptr_t) hdr;
    }

    /* Not found */
    return 0;
}
