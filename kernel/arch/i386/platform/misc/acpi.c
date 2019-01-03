#include <core/system.h>
#include <core/string.h>

#include <cpu/cpu.h>

#include <mm/vm.h>

static inline int byte_checksum(const char *p, size_t s)
{
    int8_t ret = 0;
    while (s--) ret += (int8_t) *p++;
    return ret;
}

struct acpi_rsdp *acpi_rsdp_detect(void)
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
static char acpi_data[PAGE_SIZE] __aligned(PAGE_SIZE);
static uintptr_t acpi_rsdt = 0;
static struct vmr acpi_vmr;

#define ACPI_ADDR(phy) ((uintptr_t) acpi_data + (((uintptr_t)(phy)) & PAGE_MASK))

void acpi_setup()
{
    printk("acpi: Locating RSDP...\n");
    struct acpi_rsdp *rsdp = acpi_rsdp_detect();

    if (rsdp) {
        printk("acpi: RSDP found at %p\n", rsdp);
    } else {
        printk("acpi: RSDP not found\n");
        return;
    }

    /* Map to VM */
    uintptr_t rsdt_phy  = rsdp->rsdt;
    uintptr_t rsdt_page = rsdp->rsdt & ~PAGE_MASK;
    uintptr_t rsdt_vir  = ACPI_ADDR(rsdt_phy);

    printk("acpi: RSDT is located at %p\n", rsdt_phy);
    printk("acpi: Remapping RSDT to %p\n", rsdt_vir);

    acpi_vmr.base  = (uintptr_t) acpi_data;
    acpi_vmr.paddr = rsdt_page;
    acpi_vmr.size  = PAGE_SIZE;
    acpi_vmr.flags = VM_KRW;

    vm_map(&acpi_vmr);
    acpi_rsdt = rsdt_vir;
}

uintptr_t acpi_rsdt_find(char signature[4])
{
    struct acpi_rsdt *rsdt = (struct acpi_rsdt *) acpi_rsdt;

    /* ACPI not initalized */
    if (!rsdt)
        return 0;

    int entries = (rsdt->header.length - sizeof(rsdt->header))/sizeof(uint32_t);
    printk("entries %d\n", entries);

    for (int i = 0; i < entries; ++i) {
        struct acpi_sdt_header *hdr = (struct acpi_sdt_header *) ACPI_ADDR(rsdt->sdt[i]);

        if (!strncmp(hdr->signature, signature, 4))
            return (uintptr_t) hdr;
    }

    /* Not found */
    return 0;
}
