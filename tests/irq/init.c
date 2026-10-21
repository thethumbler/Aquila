/*
 * Interrupts, as /proc/interrupts shows them: the controller (the APIC,
 * or the 8259 with apic=off, tests/run.sh boots both), the timer ticking
 * 500 times a second, the e1000's IRQ coming when a frame does (an ARP
 * reply: a datagram to 10.0.2.2, QEMU's)
 */

#include "../lib/aqtest.h"

#include <aquila/in.h>
#include <aquila/socket.h>

static char info[2048];

static int read_info(void)
{
    int fd = SC(SYS_open, "/mnt/interrupts", O_RDONLY, 0);
    int n = fd < 0 ? 0 : SC(SYS_read, fd, info, sizeof(info) - 1);

    SC(SYS_close, fd, 0, 0);
    info[n > 0 ? n : 0] = 0;
    return n;
}

/* the count of the line starting with `key' ("timer:", "11:"), -1 */
static long count_of(const char *key)
{
    int len = aq_strlen(key);

    for (char *p = info; *p; ) {
        int match = 1;

        for (int i = 0; i < len; ++i)
            if (p[i] != key[i])
                match = 0;

        if (match) {
            long v = 0;
            p += len;

            while (*p == ' ')
                ++p;
            while (*p >= '0' && *p <= '9')
                v = v * 10 + (*p++ - '0');

            return v;
        }

        while (*p && *p != '\n')
            ++p;
        if (*p)
            ++p;
    }

    return -1;
}

void aq_main(void)
{
    static struct { char *dev, *opt; } no_args = {0, 0};

    aq_begin("irq");
    check("procfs", aq_mount("procfs", "/mnt", &no_args), 0);
    check("/proc/interrupts", read_info() > 0, 1);

    /* the APIC's timer has its own line, the PIT is IRQ 0 */
    const char *timer = count_of("timer:") >= 0 ? "timer:" : "0:";
    long t0 = count_of(timer);

    struct aquila_timespec second = {1, 0};
    SC(SYS_nanosleep, &second, 0, 0);
    read_info();

    aq_puts("timer_rate=");
    aq_putn(count_of(timer) - t0);
    aq_puts("\n");

    /* a frame back: ARP for 10.0.2.2 */
    int fd = SC(SYS_socket, AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in to = {AF_INET, 9 << 8, {0x0202000a}, {0}};
    struct aquila_socket_io_args a = {fd, "x", 1, 0, &to, 0};
    unsigned int tolen = sizeof(to);
    a.addrlen = &tolen;
    check("sendto 10.0.2.2", SC(SYS_send, &a, 0, 0), 1);

    struct aquila_timespec short_wait = {0, 300000000};
    SC(SYS_nanosleep, &short_wait, 0, 0);
    read_info();

    aq_puts("--- /proc/interrupts\n");
    aq_puts(info);
    aq_puts("---\n");

    aq_end();
}
