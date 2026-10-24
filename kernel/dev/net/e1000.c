/** \file
 *          Intel 8254x (e1000) and 82574L Ethernet controllers
 *
 * References:
 *      [1] - PCI/PCI-X Family of Gigabit Ethernet Controllers Software
 *            Developer's Manual (8254x), Intel, 317453
 *      [2] - 82574 GbE Controller Family Datasheet, Intel, 317694
 *
 * The 82540EM is QEMU's default NIC on x86. Frames are received into the
 * buffers of a ring of descriptors the controller fills and interrupts
 * for, and are sent from another ring; both rings and their buffers are
 * in pages of memory, reached through the direct map, their physical
 * addresses given to the controller. A received frame is copied into a
 * netbuf and given to the stack, a frame to send is copied into a
 * transmit buffer.
 *
 * The 82574L (QEMU's e1000e) is driven the same (its legacy descriptors);
 * it interrupts with MSI, when there's an APIC to take it.
 */

#include <core/system.h>
#include <core/arch.h>
#include <core/module.h>
#include <core/printk.h>
#include <core/string.h>

#include <cpu/io.h>
#include <dev/pci.h>
#include <mm/mm.h>
#include <net/netif.h>
#include <dev/irq.h>

#include <aquila/errno.h>

/* registers */
#define E1000_CTRL      0x0000
#define E1000_STATUS    0x0008
#define E1000_EERD      0x0014
#define E1000_ICR       0x00C0
#define E1000_IMS       0x00D0
#define E1000_IMC       0x00D8
#define E1000_RCTL      0x0100
#define E1000_TCTL      0x0400
#define E1000_TIPG      0x0410
#define E1000_RDBAL     0x2800
#define E1000_RDBAH     0x2804
#define E1000_RDLEN     0x2808
#define E1000_RDH       0x2810
#define E1000_RDT       0x2818
#define E1000_TDBAL     0x3800
#define E1000_TDBAH     0x3804
#define E1000_TDLEN     0x3808
#define E1000_TDH       0x3810
#define E1000_TDT       0x3818
#define E1000_MTA       0x5200  /* 128 registers */
#define E1000_RAL       0x5400
#define E1000_RAH       0x5404

#define CTRL_ASDE       (1 << 5)    /* auto-speed detection */
#define CTRL_SLU        (1 << 6)    /* set link up */
#define CTRL_RST        (1 << 26)

#define STATUS_LU       (1 << 1)    /* link up */

#define EERD_START      (1 << 0)
#define EERD_DONE       (1 << 4)

#define ICR_TXDW        (1 << 0)
#define ICR_LSC         (1 << 2)    /* link status change */
#define ICR_RXDMT0      (1 << 4)    /* receive descriptors running low */
#define ICR_RXO         (1 << 6)    /* receiver overrun */
#define ICR_RXT0        (1 << 7)    /* receiver timer: frames came */

#define RCTL_EN         (1 << 1)
#define RCTL_BAM        (1 << 15)   /* broadcasts */
#define RCTL_SECRC      (1 << 26)   /* strip the CRC */
                                    /* BSIZE 0: buffers of 2048 bytes */

#define TCTL_EN         (1 << 1)
#define TCTL_PSP        (1 << 3)    /* pad short packets */
#define TCTL_CT         (0x0F << 4) /* collision threshold */
#define TCTL_COLD       (0x40 << 12)/* collision distance, full duplex */

#define RAH_AV          (1U << 31)  /* the address is valid */

struct rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __packed;

struct tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __packed;

#define DESC_DD         (1 << 0)    /* descriptor done */
#define DESC_EOP        (1 << 1)    /* end of packet (rx status) */

#define TXCMD_EOP       (1 << 0)
#define TXCMD_IFCS      (1 << 1)    /* insert the CRC */
#define TXCMD_RS        (1 << 3)    /* report status (DD) */

#define RX_DESCS        32
#define TX_DESCS        32
#define BUF_SIZE        2048

static struct e1000 {
    struct pci_dev pci;
    struct ioaddr regs;
    uint8_t irq;

    volatile struct rx_desc *rx;
    volatile struct tx_desc *tx;
    uint8_t *rx_buf[RX_DESCS], *tx_buf[TX_DESCS];
    unsigned rx_next;       /* the next descriptor the controller fills */
    unsigned tx_next;       /* the next descriptor to send with */

    struct netif netif;
} e1000;

/* the PCI devices (8086:xxxx) of the family */
static const uint16_t device_ids[] = {
    0x100E,     /* 82540EM (QEMU's e1000) */
    0x10D3,     /* 82574L (QEMU's e1000e): MSI */
    0x100F,     /* 82545EM copper */
    0x1004,     /* 82543GC copper */
    0x1015,     /* 82540EM LOM */
    0x1026,     /* 82545GM copper */
    0x1076,     /* 82541GI */
    0x107C,     /* 82541PI */
};

static inline uint32_t reg_read(uint32_t reg)
{
    return io_in32(&e1000.regs, reg);
}

static inline void reg_write(uint32_t reg, uint32_t val)
{
    io_out32(&e1000.regs, reg, val);
}

/** a page of memory: its physical address, its kernel address in *virt
 * (the rings and buffers, once, at boot: the kernel can't boot without
 * that little memory) */
static paddr_t page_get(void **virt)
{
    struct vm_page *vm_page = mm_page_alloc();

    if (!vm_page || !(*virt = arch_phys_map(vm_page->paddr, PAGE_SIZE)))
        panic("e1000: no memory for its rings");

    memset(*virt, 0, PAGE_SIZE);
    return vm_page->paddr;
}

/** a word of the EEPROM */
static uint16_t eeprom_read(uint8_t addr)
{
    reg_write(E1000_EERD, EERD_START | (uint32_t) addr << 8);

    for (int i = 0; i < 100000; ++i) {
        uint32_t v = reg_read(E1000_EERD);

        if (v & EERD_DONE)
            return v >> 16;
    }

    return 0;
}

static void mac_read(uint8_t mac[6])
{
    uint32_t ral = reg_read(E1000_RAL), rah = reg_read(E1000_RAH);

    if (rah & RAH_AV) {
        for (int i = 0; i < 4; ++i)
            mac[i] = ral >> (8 * i);

        mac[4] = rah;
        mac[5] = rah >> 8;
        return;
    }

    for (int i = 0; i < 3; ++i) {
        uint16_t w = eeprom_read(i);
        mac[2 * i]     = w;
        mac[2 * i + 1] = w >> 8;
    }

    /* what the receiver takes */
    reg_write(E1000_RAL, mac[0] | mac[1] << 8 | mac[2] << 16 | (uint32_t) mac[3] << 24);
    reg_write(E1000_RAH, mac[4] | mac[5] << 8 | RAH_AV);
}

static void rx_init(void)
{
    void *ring;
    paddr_t ring_paddr = page_get(&ring);

    e1000.rx = ring;

    for (int i = 0; i < RX_DESCS; i += PAGE_SIZE / BUF_SIZE) {
        void *page;
        paddr_t paddr = page_get(&page);

        for (int j = 0; j < PAGE_SIZE / BUF_SIZE; ++j) {
            e1000.rx_buf[i + j] = (uint8_t *) page + j * BUF_SIZE;
            e1000.rx[i + j].addr = paddr + j * BUF_SIZE;
        }
    }

    reg_write(E1000_RDBAL, (uint32_t) ring_paddr);
    reg_write(E1000_RDBAH, (uint32_t) ((uint64_t) ring_paddr >> 32));
    reg_write(E1000_RDLEN, RX_DESCS * sizeof(struct rx_desc));
    reg_write(E1000_RDH, 0);
    reg_write(E1000_RDT, RX_DESCS - 1);     /* all of them are the controller's */

    e1000.rx_next = 0;

    reg_write(E1000_RCTL, RCTL_EN | RCTL_BAM | RCTL_SECRC);
}

static void tx_init(void)
{
    void *ring;
    paddr_t ring_paddr = page_get(&ring);

    e1000.tx = ring;

    for (int i = 0; i < TX_DESCS; i += PAGE_SIZE / BUF_SIZE) {
        void *page;
        paddr_t paddr = page_get(&page);

        for (int j = 0; j < PAGE_SIZE / BUF_SIZE; ++j) {
            e1000.tx_buf[i + j] = (uint8_t *) page + j * BUF_SIZE;
            e1000.tx[i + j].addr = paddr + j * BUF_SIZE;
            e1000.tx[i + j].status = DESC_DD;   /* free */
        }
    }

    reg_write(E1000_TDBAL, (uint32_t) ring_paddr);
    reg_write(E1000_TDBAH, (uint32_t) ((uint64_t) ring_paddr >> 32));
    reg_write(E1000_TDLEN, TX_DESCS * sizeof(struct tx_desc));
    reg_write(E1000_TDH, 0);
    reg_write(E1000_TDT, 0);

    e1000.tx_next = 0;

    reg_write(E1000_TCTL, TCTL_EN | TCTL_PSP | TCTL_CT | TCTL_COLD);
    reg_write(E1000_TIPG, 10 | 8 << 10 | 6 << 20);
}

static int e1000_transmit(struct netif *netif, struct netbuf *nb)
{
    unsigned i = e1000.tx_next;
    volatile struct tx_desc *desc = &e1000.tx[i];

    if (nb->len > BUF_SIZE)
        return -EMSGSIZE;

    /* the ring is full: the controller hasn't sent this one yet */
    if (!(desc->status & DESC_DD)) {
        netif->stats.tx_dropped++;
        return -ENOBUFS;
    }

    memcpy(e1000.tx_buf[i], nb->data, nb->len);

    desc->length = nb->len;
    desc->cmd    = TXCMD_EOP | TXCMD_IFCS | TXCMD_RS;
    desc->status = 0;

    e1000.tx_next = (i + 1) % TX_DESCS;

    /* the descriptor written before the controller is told */
    asm volatile("" ::: "memory");
    reg_write(E1000_TDT, e1000.tx_next);

    return 0;
}

/** the frames the controller received, to the stack */
static void rx_poll(void)
{
    for (;;) {
        unsigned i = e1000.rx_next;
        volatile struct rx_desc *desc = &e1000.rx[i];

        if (!(desc->status & DESC_DD))
            break;

        size_t len = desc->length;

        /* frames that span buffers (longer than 2048 bytes) aren't taken */
        if ((desc->status & DESC_EOP) && !desc->errors && len <= NETBUF_SIZE - 256) {
            struct netbuf *nb = netbuf_alloc();

            if (nb) {
                memcpy(netbuf_put(nb, len), e1000.rx_buf[i], len);
                netif_input(&e1000.netif, nb);
            } else {
                e1000.netif.stats.rx_dropped++;
            }
        } else {
            e1000.netif.stats.rx_errors++;
        }

        /* the descriptor is the controller's again */
        desc->status = 0;
        e1000.rx_next = (i + 1) % RX_DESCS;

        asm volatile("" ::: "memory");
        reg_write(E1000_RDT, i);
    }
}

static void link_check(void)
{
    if (reg_read(E1000_STATUS) & STATUS_LU)
        e1000.netif.flags |= NETIF_RUNNING;
    else
        e1000.netif.flags &= ~NETIF_RUNNING;
}

static void e1000_irq(void *arg __unused)
{
    /* reading it acknowledges them */
    uint32_t icr = reg_read(E1000_ICR);

    if (icr & ICR_LSC)
        link_check();

    if (icr & (ICR_RXT0 | ICR_RXDMT0 | ICR_RXO))
        rx_poll();
}

static int e1000_probe(void)
{
    struct pci_bar bar;
    size_t n = sizeof(device_ids) / sizeof(device_ids[0]);
    size_t i;

    for (i = 0; i < n; ++i)
        if (!pci_device_scan(0x8086, device_ids[i], &e1000.pci))
            break;

    if (i == n)
        return 0;

    if (pci_bar(&e1000.pci, 0, &bar) || bar.io) {
        printk("e1000: no registers (BAR 0)\n");
        return -1;
    }

    pci_enable(&e1000.pci);

    /* registers at byte offsets (MMIO16 and MMIO32 space them out), read
     * and written 32 bits at a time */
    e1000.regs.type = IOADDR_MMIO8;
    e1000.regs.addr = (uintptr_t) arch_mmio_map(bar.addr, bar.size);

    /* reset, no interrupts until all is set up */
    reg_write(E1000_IMC, 0xFFFFFFFF);
    reg_write(E1000_CTRL, reg_read(E1000_CTRL) | CTRL_RST);

    for (int t = 0; t < 100000 && (reg_read(E1000_CTRL) & CTRL_RST); ++t)
        ;

    reg_write(E1000_IMC, 0xFFFFFFFF);
    reg_write(E1000_CTRL, reg_read(E1000_CTRL) | CTRL_SLU | CTRL_ASDE);

    mac_read(e1000.netif.mac);

    /* no multicast */
    for (int m = 0; m < 128; ++m)
        reg_write(E1000_MTA + 4 * m, 0);

    rx_init();
    tx_init();

    e1000.netif.transmit = e1000_transmit;
    e1000.netif.priv = &e1000;
    link_check();

    netif_register(&e1000.netif);

    /* its interrupts as messages if it can (MSI), else its line */
    int irq = pci_msi_enable(&e1000.pci, e1000_irq, NULL);
    int msi = irq >= 0;

    if (!msi && ((irq = pci_irq(&e1000.pci)) < 0 || irq_install(irq, e1000_irq, NULL)))
        printk("e1000: no interrupt\n");

    e1000.irq = irq;

    printk("e1000: %x:%x at %p, irq %d%s, link %s\n", 0x8086, device_ids[i],
           (void *) (uintptr_t) bar.addr, e1000.irq, msi ? " (msi)" : "",
           e1000.netif.flags & NETIF_RUNNING ? "up" : "down");

    reg_read(E1000_ICR);
    reg_write(E1000_IMS, ICR_LSC | ICR_RXT0 | ICR_RXDMT0 | ICR_RXO);

    return 0;
}

MODULE_INIT(e1000, e1000_probe, NULL)
