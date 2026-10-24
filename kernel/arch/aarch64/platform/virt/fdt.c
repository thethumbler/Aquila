/** \file
 * Reading the devicetree that the boot loader passes (a flattened one, an
 * FDT) into the kernel's struct boot: the RAM (/memory's reg property),
 * the initrd (/chosen's linux,initrd-start and linux,initrd-end), and the
 * command line (/chosen's bootargs).
 *
 * References:
 *      [1] - Devicetree Specification, release 0.4, chapter 5
 *            (Flattened Devicetree Format)
 *
 * The blob is big-endian: a header, then the structure (tokens: a node
 * begins, a property, a node ends), then the strings (the properties'
 * names). Only what the kernel needs is read. The root's `#address-cells`
 * and `#size-cells` say how many 32-bit cells an address and a size take.
 */

#include <core/system.h>
#include <core/string.h>
#include <boot/boot.h>
#include <mm/mm.h>

/* The FDT's magic number, and its structure's tokens. */
#define FDT_MAGIC       0xD00DFEED
#define FDT_BEGIN_NODE  1
#define FDT_END_NODE    2
#define FDT_PROP        3
#define FDT_NOP         4
#define FDT_END         9

/** The FDT's header (big-endian). */
struct fdt_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

/** Reads a big-endian 32-bit number. */
static inline uint32_t be32(const void *p)
{
    const uint8_t *b = p;
    return (uint32_t) b[0] << 24 | (uint32_t) b[1] << 16 | (uint32_t) b[2] << 8 | b[3];
}

/** Reads `cells` big-endian 32-bit cells at `p` as one number. */
static uint64_t cells(const void *p, int cells)
{
    uint64_t v = 0;

    for (int i = 0; i < cells; ++i)
        v = v << 32 | be32((const uint8_t *) p + 4 * i);

    return v;
}

/* The most memory regions that are kept. */
#define MMAP_MAX    16

/* What the devicetree says, in the kernel's form. */
static struct boot boot;
static mmap_t mmap[MMAP_MAX];
static module_t modules[1];
static char cmdline[1024];

/**
 * Reads the boot loader's devicetree into a struct boot. If there is no
 * devicetree there, the struct boot is empty.
 *
 * @param dtb  the devicetree's physical address
 * @return the struct boot (static)
 */
struct boot *fdt_boot(uintptr_t dtb)
{
    const struct fdt_header *h = PHYS2VIRT(dtb);

    boot.cmdline = cmdline;
    boot.mmap = mmap;
    boot.modules = modules;

    if (be32(&h->magic) != FDT_MAGIC) {
        printk("fdt: no devicetree at %p\n", (void *) dtb);
        return &boot;
    }

    const uint8_t *structs = (const uint8_t *) h + be32(&h->off_dt_struct);
    const char *strings = (const char *) h + be32(&h->off_dt_strings);

    int addr_cells = 2, size_cells = 2, depth = 0;
    int in_memory = 0, in_chosen = 0;
    uint64_t initrd_start = 0, initrd_end = 0;

    for (const uint8_t *p = structs;;) {
        uint32_t token = be32(p);
        p += 4;

        if (token == FDT_BEGIN_NODE) {
            const char *name = (const char *) p;
            size_t len = strlen(name);
            p += (len + 1 + 3) & ~3;
            ++depth;

            /* /memory or /memory@40000000, and /chosen */
            in_memory = depth == 2 && !strncmp(name, "memory", 6) && (name[6] == '@' || !name[6]);
            in_chosen = depth == 2 && !strcmp(name, "chosen");
        } else if (token == FDT_END_NODE) {
            --depth;
            in_memory = in_chosen = 0;
        } else if (token == FDT_PROP) {
            uint32_t len = be32(p), nameoff = be32(p + 4);
            const uint8_t *val = p + 8;
            const char *name = strings + nameoff;
            p += 8 + ((len + 3) & ~3);

            if (depth == 1 && !strcmp(name, "#address-cells"))
                addr_cells = be32(val);
            else if (depth == 1 && !strcmp(name, "#size-cells"))
                size_cells = be32(val);
            else if (in_memory && !strcmp(name, "reg")) {
                size_t entry = 4 * (addr_cells + size_cells);

                for (size_t off = 0; off + entry <= len && boot.mmap_count < MMAP_MAX; off += entry) {
                    uint64_t start = cells(val + off, addr_cells);
                    uint64_t size = cells(val + off + 4 * addr_cells, size_cells);

                    mmap[boot.mmap_count++] = (mmap_t) {
                        .type  = MMAP_USABLE,
                        .start = start,
                        .end   = start + size,
                    };

                    boot.total_mem += size / 1024;
                }
            } else if (in_chosen && !strcmp(name, "bootargs")) {
                size_t n = MIN(len, sizeof(cmdline) - 1);
                memcpy(cmdline, val, n);
                cmdline[n] = 0;
            } else if (in_chosen && !strcmp(name, "linux,initrd-start")) {
                initrd_start = cells(val, len / 4);
            } else if (in_chosen && !strcmp(name, "linux,initrd-end")) {
                initrd_end = cells(val, len / 4);
            }
        } else if (token == FDT_NOP) {
            continue;
        } else {
            break;  /* FDT_END, or a token it doesn't know */
        }
    }

    /* the initrd is the one module, as Multiboot's first one is */
    if (initrd_end > initrd_start) {
        modules[0] = (module_t) {
            .paddr   = initrd_start,
            .size    = initrd_end - initrd_start,
            .cmdline = "",
        };

        boot.modules_count = 1;
    }

    return &boot;
}
