#include <core/system.h>
#include <core/string.h>
#include <core/kargs.h>
#include <cpu/io.h>
#include <platform/misc.h>

static struct ioaddr pit_ioaddr;

#define PIT_CHANNEL0    0x00
#define PIT_CMD         0x03

struct pit_cmd_register {
    union {
        struct {
            uint32_t bcd    : 1;
            uint32_t mode   : 3;
            uint32_t access : 2;
            uint32_t channel: 2;
        } __packed;
        uint8_t raw;
    } __packed;
} __packed;

#define PIT_MODE_RATE_GENERATOR 0x2
#define PIT_MODE_SQUARE_WAVE    0x3
#define PIT_ACCESS_LOHIBYTE     0x3

int x86_pit_setup(struct ioaddr *io)
{
    printk("i8254: Initializing [%p (%s)]\n", io->addr, ioaddr_type_str(io));
    pit_ioaddr = *io;
    return 0;
}

#define FBASE   1193182ULL  /* PIT Oscillator operates at 1.193182 MHz */

static uint32_t atou32(const char *s)
{
    uint32_t ret = 0;

    while (*s) {
        ret = ret * 10 + (*s - '0');
        ++s;
    }

    return ret;
}

uint32_t x86_pit_period_set(uint32_t period_ns)
{
    printk("i8254: requested period %d ns\n", period_ns);

    uint32_t div;
    const char *arg_div = NULL;

    if (!kargs_get("i8254.div", &arg_div)) {
        div = atou32(arg_div);
    } else {
        //uint32_t period_ms = period_ns / 1000UL;
        //uint32_t freq = 1000000000UL/period_ms;
        //printk("freq = %d Hz\n", freq);
        //div = FBASE/freq;  
        div = period_ns/838UL;
    }

    if (div == 0) div = 1;

    period_ns = 1000000000UL/(FBASE/div);

    printk("i8254: Setting period to %d ns (div = %d)\n", period_ns, div);

    struct pit_cmd_register cmd = {
        .bcd = 0,
        .mode = PIT_MODE_SQUARE_WAVE,
        .access = PIT_ACCESS_LOHIBYTE,
        .channel = 0,
    };

    io_out8(&pit_ioaddr, PIT_CMD, cmd.raw);
    io_out8(&pit_ioaddr, PIT_CHANNEL0, (div >> 0) & 0xFF);
    io_out8(&pit_ioaddr, PIT_CHANNEL0, (div >> 8) & 0xFF);

    return period_ns;
}
