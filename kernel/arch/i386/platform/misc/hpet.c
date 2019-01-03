#include <core/system.h>
#include <mm/mm.h>
#include <cpu/cpu.h>
#include <cpu/io.h>

#define HPET_MEM_IO     0
#define HPET_PORT_IO    1

#define HPET_REG_CAP           (0x000)
#define HPET_REG_CNF           (0x010)
#define HPET_REG_INT           (0x020)
#define HPET_REG_CNT           (0x0F0)
#define HPET_REG_TMR_CNF(n)    (0x100 + 0x20 * (n))
#define HPET_REG_TMR_CMP(n)    (0x108 + 0x20 * (n))
#define HPET_REG_TMR_INT(n)    (0x110 + 0x20 * (n))

struct hpet_reg_cap {
    union {
        struct {
            uint32_t rev_id: 8;
            uint32_t num_tim: 5;
            uint32_t count_size: 1;
            uint32_t : 1;
            uint32_t legacy: 1;
            uint32_t vendor_id: 16;
            uint32_t counter_clk_period: 32;
        } __packed;
        uint64_t value;
    } __packed;
} __packed;

struct hpet_reg_cnf {
    union {
        struct {
            uint32_t enable: 1;
            uint32_t legacy: 1;
            uint64_t : 62;
        } __packed;
        uint64_t value;
    } __packed;
} __packed;

#define _TMR_ENB _BV(0)
#define _LEGACY  _BV(1)

struct hpet_reg_tmr_cnf {
    union {
        struct {
            uint32_t : 1;
            uint32_t cnf_int_type: 1;
            uint32_t cnf_int_enb: 1;
            uint32_t cnf_type: 1;
            uint32_t cap_per: 1;
            uint32_t cap_size: 1;
            uint32_t cnf_val_set: 1;
            uint32_t : 1;
            uint32_t cnf_32mode: 1;
            uint32_t cnf_route: 5;
            uint32_t cnf_fsb_en: 1;
            uint32_t cap_fsb_del: 1;
            uint32_t : 16;
            uint32_t cap_route: 32;
        } __packed;
        uint64_t value;
    } __packed;
} __packed;

#define _INT_ENB  _BV(2)
#define _PERIODIC _BV(3)
#define _SET_VAL  _BV(6)

struct hpet {
    struct acpi_sdt_header header;
    uint8_t  hardware_rev_id;
    uint8_t  comparator_count: 5;
    uint8_t  counter_size: 1;
    uint8_t  reserved: 1;
    uint8_t  legacy_replacement: 1;
    uint16_t pci_vendor_id;

    struct {
        uint8_t  type;
        uint8_t  register_bit_width;
        uint8_t  register_bit_offset;
        uint8_t  reserved;
        uint64_t address;
    } address __packed;

    uint8_t  hpet_number;
    uint16_t minimum_tick;
    uint8_t  page_protection;
} __packed;

static char hpet_data[PAGE_SIZE] __aligned(PAGE_SIZE);
static uint32_t hpet_timer_period = 0;
static size_t   hpet_timer_id = 0;
static struct   hpet *hpet = NULL;
static void (*hpet_handler)(void);

#define HPET_ADDR(off) ((uintptr_t)hpet_data + (off))
#define HPET_REG(off) ((uint32_t) hpet->address.address + (off))

static uint64_t hpet_reg_read(uint16_t off)
{
    if (hpet->address.type == HPET_PORT_IO)
        //return inq(HPET_REG(off));
        return 0;
    else
        return __mmio_inq(HPET_ADDR(off));
}

static void hpet_reg_write(uint16_t off, uint64_t val)
{
    if (hpet->address.type == HPET_PORT_IO)
        //return inq(HPET_REG(off));
        return;
    else
        __mmio_outq(HPET_ADDR(off), val);
}

void hpet_irq_handler(void)
{
    if (hpet_handler)
        hpet_handler();
}

int hpet_timer_setup(size_t period_ns, void (*handler)())
{
    hpet_handler = handler;

    /* Initialize timer */
    uint64_t period_fs = period_ns * 1000000ULL;
    size_t n = hpet_timer_id;

    printk("HPET: Setting timer %d with period %ld fs\n", n, period_fs);

    hpet_reg_write(HPET_REG_TMR_CNF(n), _INT_ENB | _PERIODIC | _SET_VAL);
    hpet_reg_write(HPET_REG_TMR_CMP(n), hpet_reg_read(HPET_REG_CNT) + period_fs);
    hpet_reg_write(HPET_REG_TMR_CMP(n), period_fs);

    //irq_install_handler(0, hpet_irq_handler);

    hpet_reg_write(HPET_REG_CNF, _TMR_ENB | _LEGACY);
    return 0;
}

void hpet_setup()
{
    uintptr_t hpet_addr = acpi_rsdt_find("HPET");

    printk("HPET: Found at %p\n", hpet_addr);
    hpet = (struct hpet *) hpet_addr;

    if (hpet->address.type == HPET_MEM_IO) {
        printk("HPET: Mem I/O\n");
        printk("HPET: Addr %p\n", hpet->address.address);
        printk("HPET: Remappng addr to %p\n", hpet_data);
        //pmman.map_to(__hpet->address.address, (uintptr_t) hpet_data, PAGE_SIZE, KRW);
    }

    struct hpet_reg_cap cap;
    cap.value = hpet_reg_read(HPET_REG_CAP);
    hpet_timer_period = cap.counter_clk_period;

    size_t tmrs_cnt = cap.num_tim;
    for (size_t i = 0; i < tmrs_cnt; ++i) {
        struct hpet_reg_tmr_cnf tcnf;
        tcnf.value = hpet_reg_read(HPET_REG_TMR_CNF(i));
        if (tcnf.cap_per) {
            hpet_timer_id = i;
            printk("HPET: Using timer %d\n", i);
            break;
        }
    }

    //hpet_timer_setup(500ULL * 1000 * 1000, NULL);
}
