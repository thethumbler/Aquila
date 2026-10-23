/** \file
 * ACPI: its tables (from the RSDT, or the XSDT of ACPI 2.0), mapped once
 * and found by their signature (x86_acpi_table: HPET, ...); and powering
 * off: the S5 sleep state, with the PM1 control registers of the FADT
 * and the sleep type of the \_S5 object of the DSDT, found by its name
 * without an AML interpreter.
 */

#include <core/system.h>
#include <core/string.h>

#include <cpu/cpu.h>
#include <cpu/io.h>
#include <cpu/sdt.h>

#include <mm/mm.h>
#include <mm/vm.h>
#include <mm/kvmem.h>

#include <platform/misc.h>

MALLOC_DEFINE(M_ACPI, "acpi", "ACPI table mapping");

/* where tables are mapped */
#define ACPI_WINDOW         ARCH_ACPI_BASE
#define ACPI_WINDOW_SIZE    0x00400000UL

static uintptr_t window_next = ACPI_WINDOW;

/* the FADT fields used here */
#define FADT_DSDT           40
#define FADT_SMI_CMD        48
#define FADT_ACPI_ENABLE    52
#define FADT_PM1A_CNT       64
#define FADT_PM1B_CNT       68

#define PM1_SCI_EN          0x0001
#define PM1_SLP_EN          0x2000

/* AML opcodes */
#define AML_NAME            0x08
#define AML_BYTE_PREFIX     0x0A
#define AML_PACKAGE         0x12

static int checksum(const void *p, size_t size)
{
    uint8_t sum = 0;

    for (size_t i = 0; i < size; ++i)
        sum += ((const uint8_t *) p)[i];

    return sum;
}

/** maps physical memory, NULL if the window is full */
static void *acpi_map(uintptr_t paddr, size_t size)
{
    uintptr_t page = PAGE_ALIGN(paddr);
    size_t len = PAGE_ROUND(paddr + size) - page;

    if (window_next + len > ACPI_WINDOW + ACPI_WINDOW_SIZE)
        return NULL;

    struct vm_entry *entry = kmalloc(sizeof(*entry), &M_ACPI, M_ZERO);
    if (!entry) return NULL;

    entry->base  = window_next;
    entry->paddr = page;
    entry->size  = len;
    entry->flags = VM_KR;

    if (vm_map(&kvm_space, entry)) {
        kfree(entry);
        return NULL;
    }

    window_next += len;
    return (void *) (entry->base + (paddr - page));
}

/** a table, mapped as long as its header says it is, if it checks out */
static struct acpi_sdt_header *acpi_table(uintptr_t paddr)
{
    struct acpi_sdt_header *hdr = acpi_map(paddr, sizeof(*hdr));

    if (!hdr || hdr->length < sizeof(*hdr) || hdr->length > ACPI_WINDOW_SIZE / 2)
        return NULL;

    hdr = acpi_map(paddr, hdr->length);

    return hdr && !checksum(hdr, hdr->length) ? hdr : NULL;
}

/** the RSDP is in the first KiB of the EBDA or in the BIOS ROM */
static struct acpi_rsdp *acpi_rsdp(void)
{
    uintptr_t ebda = (uintptr_t) *(uint16_t *) VMA(0x40E) << 4;
    uintptr_t areas[][2] = {{ebda, ebda + 0x400}, {0xE0000, 0x100000}};

    for (int i = 0; i < 2; ++i) {
        for (uintptr_t p = areas[i][0]; p + sizeof(struct acpi_rsdp) <= areas[i][1]; p += 16) {
            struct acpi_rsdp *rsdp = (struct acpi_rsdp *) VMA(p);

            if (!memcmp(rsdp->signature, "RSD PTR ", 8) && !checksum(rsdp, sizeof(*rsdp)))
                return rsdp;
        }
    }

    return NULL;
}

#define ACPI_TABLES_MAX     32

static struct acpi_sdt_header *tables[ACPI_TABLES_MAX];
static int tables_nr = -1;

/** every table the RSDT (or XSDT) lists, mapped */
static void acpi_tables(void)
{
    struct acpi_rsdp *rsdp = acpi_rsdp();
    struct acpi_rsdp2 *rsdp2 = (struct acpi_rsdp2 *) rsdp;

    tables_nr = 0;

    if (!rsdp) {
        printk("acpi: no RSDP\n");
        return;
    }

    /* the XSDT, if there's one (and we can reach it) */
    if (rsdp->rev >= 2 && !checksum(rsdp2, sizeof(*rsdp2)) && rsdp2->xsdt &&
        rsdp2->xsdt < ((uint64_t) 1 << (sizeof(uintptr_t) * 8 - 1))) {
        struct acpi_xsdt *xsdt = (struct acpi_xsdt *) acpi_table((uintptr_t) rsdp2->xsdt);

        if (xsdt) {
            size_t n = (xsdt->header.length - sizeof(xsdt->header)) / sizeof(uint64_t);

            for (size_t i = 0; i < n && tables_nr < ACPI_TABLES_MAX; ++i) {
                struct acpi_sdt_header *hdr = acpi_table((uintptr_t) xsdt->sdt[i]);

                if (hdr)
                    tables[tables_nr++] = hdr;
            }

            return;
        }
    }

    struct acpi_rsdt *rsdt = (struct acpi_rsdt *) acpi_table(rsdp->rsdt);

    if (!rsdt)
        return;

    size_t n = (rsdt->header.length - sizeof(rsdt->header)) / sizeof(uint32_t);

    for (size_t i = 0; i < n && tables_nr < ACPI_TABLES_MAX; ++i) {
        struct acpi_sdt_header *hdr = acpi_table(rsdt->sdt[i]);

        if (hdr)
            tables[tables_nr++] = hdr;
    }
}

/** the table of that signature, NULL if there's none */
void *x86_acpi_table(const char *signature)
{
    if (tables_nr < 0)
        acpi_tables();

    for (int i = 0; i < tables_nr; ++i)
        if (!memcmp(tables[i]->signature, signature, 4))
            return tables[i];

    return NULL;
}

/** an integer element of a package: BytePrefix n, or ZeroOp and OneOp */
static int aml_int(const uint8_t **p, const uint8_t *end)
{
    if (*p >= end)
        return -1;

    if (**p == AML_BYTE_PREFIX && *p + 1 < end) {
        *p += 2;
        return (*p)[-1];
    }

    if (**p <= 1)
        return *(*p)++;

    return -1;
}

/**
 * The sleep types of S5: Name (_S5, Package () {a, b, ...}) in the DSDT,
 * -1 if there isn't one.
 */
static int acpi_s5(struct acpi_sdt_header *dsdt, int *typb)
{
    const uint8_t *aml = (const uint8_t *) dsdt;
    const uint8_t *end = aml + dsdt->length;

    for (const uint8_t *p = aml + sizeof(*dsdt); p + 8 < end; ++p) {
        if (memcmp(p, "_S5_", 4))
            continue;

        /* a Name, \_S5_ or _S5_, of a package */
        if (!(p[-1] == AML_NAME || (p[-1] == '\\' && p[-2] == AML_NAME)) || p[4] != AML_PACKAGE)
            continue;

        /* the package length takes 1 to 4 bytes, the element count 1 */
        const uint8_t *q = p + 5;
        q += ((*q >> 6) & 3) + 1;
        q++;

        int typa = aml_int(&q, end);
        *typb = aml_int(&q, end);

        if (typa >= 0)
            return typa;
    }

    return -1;
}

static void pm1_write(uint32_t port, uint16_t val)
{
    struct ioaddr io = {.addr = port, .type = IOADDR_PORT};
    io_out16(&io, 0, val);
}

static uint16_t pm1_read(uint32_t port)
{
    struct ioaddr io = {.addr = port, .type = IOADDR_PORT};
    return io_in16(&io, 0);
}

/** powers off, returns if it can't */
void x86_acpi_poweroff(void)
{
    uint8_t *fadt = x86_acpi_table("FACP");

    if (!fadt) {
        printk("acpi: no FADT\n");
        return;
    }

    struct acpi_sdt_header *dsdt = acpi_table(*(uint32_t *) (fadt + FADT_DSDT));
    uint32_t smi_cmd  = *(uint32_t *) (fadt + FADT_SMI_CMD);
    uint8_t  enable   = fadt[FADT_ACPI_ENABLE];
    uint32_t pm1a_cnt = *(uint32_t *) (fadt + FADT_PM1A_CNT);
    uint32_t pm1b_cnt = *(uint32_t *) (fadt + FADT_PM1B_CNT);
    int typa, typb = -1;

    if (!dsdt || !pm1a_cnt || (typa = acpi_s5(dsdt, &typb)) < 0) {
        printk("acpi: no S5 sleep state\n");
        return;
    }

    /* ACPI mode, if the firmware isn't in it */
    if (!(pm1_read(pm1a_cnt) & PM1_SCI_EN) && smi_cmd && enable) {
        struct ioaddr io = {.addr = smi_cmd, .type = IOADDR_PORT};
        io_out8(&io, 0, enable);

        for (int i = 0; i < 1000000 && !(pm1_read(pm1a_cnt) & PM1_SCI_EN); ++i)
            ;
    }

    pm1_write(pm1a_cnt, typa << 10 | PM1_SLP_EN);

    if (pm1b_cnt && typb >= 0)
        pm1_write(pm1b_cnt, typb << 10 | PM1_SLP_EN);

    /* it takes a moment */
    for (volatile int i = 0; i < 10000000; ++i)
        ;

    printk("acpi: S5 sleep state didn't power off\n");
}
