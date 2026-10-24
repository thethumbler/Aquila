/** \file
 * x86: the 8254 programmable interval timer (PIT).
 *
 * Its channel 0 is the scheduler's tick when there's no APIC (IRQ 0).
 * Its channel 2, normally the speaker's, measures short delays: to
 * calibrate the TSC and the local APIC's timer, and to wait while
 * starting the other CPUs.
 */

#include <core/system.h>
#include <core/string.h>
#include <core/kargs.h>
#include <cpu/io.h>
#include <platform/misc.h>

static struct ioaddr pit_ioaddr;

#define PIT_CHANNEL0    0x00
#define PIT_CHANNEL2    0x02
#define PIT_CMD         0x03

/* channel 2's gate and output, in the speaker's port */
#define PIT_GATE_PORT   0x61
#define GATE2           0x01
#define SPEAKER         0x02
#define OUT2            0x20

/** The PIT's command register: which channel, how it is loaded, and in
 * which mode it counts. */
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

/**
 * Sets where the PIT's ports are.
 *
 * @param io  its ports (0x40)
 * @return 0
 */
int x86_pit_setup(struct ioaddr *io)
{
    printk("i8254: Initializing [%p (%s)]\n", io->addr, ioaddr_type_str(io));
    pit_ioaddr = *io;
    return 0;
}

#define FBASE   1193182ULL  /* the PIT's oscillator runs at 1.193182 MHz */

/** Converts a decimal string to a number. */
static uint32_t atou32(const char *s)
{
    uint32_t ret = 0;

    while (*s) {
        ret = ret * 10 + (*s - '0');
        ++s;
    }

    return ret;
}

/**
 * Makes channel 0 interrupt periodically, for the scheduler's tick. The
 * `i8254.div` kernel argument sets the divisor directly.
 *
 * @param period_ns  the period wanted, in ns
 * @return the period it is, in ns (the PIT's clock divided by a whole
 *         number)
 */
uint32_t x86_pit_period_set(uint32_t period_ns)
{
    printk("i8254: requested period %d ns\n", period_ns);

    uint32_t div;
    const char *arg_div = NULL;

    if (!kargs_get("i8254.div", &arg_div) && arg_div) {
        div = atou32(arg_div);
    } else {
        div = period_ns/838UL;
    }

    /* the divisor register is 16 bits wide */
    if (div == 0) div = 1;
    if (div > 0xFFFF) div = 0xFFFF;

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

/**
 * Waits `us` microseconds, at most 54 ms, busy. Channel 2 counts down
 * once, and its output goes high at 0; channel 0 keeps the tick.
 *
 * @param us  how long to wait, in microseconds
 */
void x86_pit_delay(uint32_t us)
{
    struct ioaddr gate = {.addr = PIT_GATE_PORT, .type = IOADDR_PORT};
    uint32_t count = (uint32_t) (FBASE * us / 1000000ULL);

    if (count > 0xFFFF)
        count = 0xFFFF;

    /* the gate off, and the speaker off */
    io_out8(&gate, 0, (io_in8(&gate, 0) & ~(GATE2 | SPEAKER)));

    struct pit_cmd_register cmd = {
        .bcd = 0,
        .mode = 0,      /* "interrupt on terminal count": count down once */
        .access = PIT_ACCESS_LOHIBYTE,
        .channel = 2,
    };

    io_out8(&pit_ioaddr, PIT_CMD, cmd.raw);
    io_out8(&pit_ioaddr, PIT_CHANNEL2, count & 0xFF);
    io_out8(&pit_ioaddr, PIT_CHANNEL2, count >> 8);

    /* the gate on: it counts */
    io_out8(&gate, 0, (io_in8(&gate, 0) & ~SPEAKER) | GATE2);

    while (!(io_in8(&gate, 0) & OUT2))
        ;

    io_out8(&gate, 0, io_in8(&gate, 0) & ~GATE2);
}
