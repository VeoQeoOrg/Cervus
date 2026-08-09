#include "../../include/net/rtl8169.h"
#include "../../include/net/netdev.h"
#include "../../include/drivers/pci.h"
#include "../../include/memory/dma.h"
#include "../../include/apic/apic.h"
#include "../../include/interrupts/irq.h"
#include "../../include/sched/sched.h"
#include "../../include/sched/spinlock.h"
#include "../../include/io/serial.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#define R_MAC0      0x00
#define R_MAR0      0x08
#define R_TNPDS     0x20
#define R_CR        0x37
#define R_TPPOLL    0x38
#define R_IMR       0x3C
#define R_ISR       0x3E
#define R_TCR       0x40
#define R_RCR       0x44
#define R_9346CR    0x50
#define R_PHYSTS    0x6C
#define R_RMS       0xDA
#define R_CPCR      0xE0
#define R_RDSAR     0xE4
#define R_MTPS      0xEC

#define CR_RST      0x10
#define CR_RE       0x08
#define CR_TE       0x04

#define C9346_UNLOCK 0xC0
#define C9346_LOCK   0x00

#define TPPOLL_NPQ   0x40

#define ISR_ROK      0x0001
#define ISR_RER      0x0002
#define ISR_TOK      0x0004
#define ISR_RXOVW    0x0010
#define ISR_LINKCHG  0x0020
#define ISR_FOVW     0x0040

#define PHYSTS_LINK  0x02

#define DESC_OWN      0x80000000u
#define DESC_EOR      0x40000000u
#define DESC_FS       0x20000000u
#define DESC_LS       0x10000000u
#define DESC_LEN_MASK 0x00003FFFu

#define RTL_NUM_RX  32
#define RTL_NUM_TX  16
#define RTL_BUFSZ   2048

typedef struct __attribute__((packed)) {
    uint32_t opts1;
    uint32_t opts2;
    uint64_t addr;
} rtl_desc_t;

typedef struct rtl8169 {
    volatile uint8_t *regs;
    netdev_t         *ndev;

    rtl_desc_t       *rx;   uintptr_t rx_phys;
    rtl_desc_t       *tx;   uintptr_t tx_phys;
    uint8_t          *rx_buf[RTL_NUM_RX];
    uint8_t          *tx_buf[RTL_NUM_TX]; uintptr_t tx_buf_phys[RTL_NUM_TX];

    uint32_t          rx_cur;
    uint32_t          tx_cur;

    int               vector;
    int               polled;
    spinlock_t        lock;

    struct rtl8169   *next;
} rtl8169_t;

static rtl8169_t *g_nics;

static inline uint8_t  r8 (rtl8169_t *r, uint32_t o) { return *(volatile uint8_t  *)(r->regs + o); }
static inline uint16_t r16(rtl8169_t *r, uint32_t o) { return *(volatile uint16_t *)(r->regs + o); }
static inline void w8 (rtl8169_t *r, uint32_t o, uint8_t  v) { *(volatile uint8_t  *)(r->regs + o) = v; }
static inline void w16(rtl8169_t *r, uint32_t o, uint16_t v) { *(volatile uint16_t *)(r->regs + o) = v; }
static inline void w32(rtl8169_t *r, uint32_t o, uint32_t v) { *(volatile uint32_t *)(r->regs + o) = v; }

static void rtl_rx_drain(rtl8169_t *r) {
    uint8_t buf[RTL_BUFSZ];
    for (;;) {
        spinlock_acquire(&r->lock);
        uint32_t i = r->rx_cur;
        uint32_t opts1 = r->rx[i].opts1;
        if (opts1 & DESC_OWN) { spinlock_release(&r->lock); return; }
        uint32_t len = opts1 & DESC_LEN_MASK;
        if (len >= 4) len -= 4;
        if (len > RTL_BUFSZ) len = RTL_BUFSZ;
        memcpy(buf, r->rx_buf[i], len);
        uint32_t eor = (i == RTL_NUM_RX - 1) ? DESC_EOR : 0;
        r->rx[i].opts1 = DESC_OWN | eor | RTL_BUFSZ;
        r->rx_cur = (i + 1) % RTL_NUM_RX;
        spinlock_release(&r->lock);

        net_rx(r->ndev, buf, len);
    }
}

static int rtl_transmit(netdev_t *nd, const void *frame, size_t len) {
    rtl8169_t *r = nd->priv;
    if (len == 0 || len > RTL_BUFSZ) return -1;

    spinlock_acquire(&r->lock);
    uint32_t i = r->tx_cur;
    if (r->tx[i].opts1 & DESC_OWN) { spinlock_release(&r->lock); return -1; }
    memcpy(r->tx_buf[i], frame, len);
    uint32_t eor = (i == RTL_NUM_TX - 1) ? DESC_EOR : 0;
    r->tx[i].addr  = r->tx_buf_phys[i];
    r->tx[i].opts2 = 0;
    r->tx[i].opts1 = DESC_OWN | DESC_FS | DESC_LS | eor | (uint32_t)len;
    r->tx_cur = (i + 1) % RTL_NUM_TX;
    w8(r, R_TPPOLL, TPPOLL_NPQ);
    spinlock_release(&r->lock);

    for (int t = 0; t < 1000000; t++)
        if (!(r->tx[i].opts1 & DESC_OWN)) break;
    return 0;
}

static void rtl_irq(void *ctx) {
    rtl8169_t *r = ctx;
    uint16_t st = r16(r, R_ISR);
    w16(r, R_ISR, st);
    rtl_rx_drain(r);
}

static void rtl_worker(void *arg) {
    (void)arg;
    for (;;) {
        for (rtl8169_t *r = g_nics; r; r = r->next)
            if (r->polled) rtl_rx_drain(r);
        task_sleep_ms(1);
    }
}

static int rtl_probe(pci_device_t *dev) {
    if (dev->device_id == 0x8139 || dev->device_id == 0x8138) return -1;

    int barx = -1;
    for (int i = 0; i < 6; i++)
        if (dev->bars[i].type == PCI_BAR_TYPE_MEM && dev->bars[i].base) { barx = i; break; }
    if (barx < 0) { serial_printf("[rtl8169] %04x: no MMIO BAR\n", dev->device_id); return -1; }

    uint16_t cmd = pci_config_read16(dev->segment, dev->bus, dev->device, dev->function, PCI_COMMAND);
    cmd |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
    pci_config_write16(dev->segment, dev->bus, dev->device, dev->function, PCI_COMMAND, cmd);

    rtl8169_t *r = calloc(1, sizeof(*r));
    if (!r) return -1;
    r->regs = (volatile uint8_t *)mmio_map(dev->bars[barx].base, dev->bars[barx].size);
    if (!r->regs) { free(r); return -1; }

    w8(r, R_CR, CR_RST);
    for (int i = 0; i < 1000000; i++) if (!(r8(r, R_CR) & CR_RST)) break;

    uint8_t mac[6];
    for (int i = 0; i < 6; i++) mac[i] = r8(r, R_MAC0 + i);

    r->rx = dma_alloc_coherent_low(RTL_NUM_RX * sizeof(rtl_desc_t), &r->rx_phys);
    r->tx = dma_alloc_coherent_low(RTL_NUM_TX * sizeof(rtl_desc_t), &r->tx_phys);
    if (!r->rx || !r->tx) { free(r); return -1; }
    memset(r->rx, 0, RTL_NUM_RX * sizeof(rtl_desc_t));
    memset(r->tx, 0, RTL_NUM_TX * sizeof(rtl_desc_t));
    for (int i = 0; i < RTL_NUM_RX; i++) {
        uintptr_t bp;
        r->rx_buf[i] = dma_alloc_coherent_low(RTL_BUFSZ, &bp);
        if (!r->rx_buf[i]) { free(r); return -1; }
        r->rx[i].addr  = bp;
        r->rx[i].opts1 = DESC_OWN | ((i == RTL_NUM_RX - 1) ? DESC_EOR : 0) | RTL_BUFSZ;
    }
    for (int i = 0; i < RTL_NUM_TX; i++) {
        r->tx_buf[i] = dma_alloc_coherent_low(RTL_BUFSZ, &r->tx_buf_phys[i]);
        if (!r->tx_buf[i]) { free(r); return -1; }
        r->tx[i].opts1 = (i == RTL_NUM_TX - 1) ? DESC_EOR : 0;
    }

    w8(r, R_9346CR, C9346_UNLOCK);
    w32(r, R_MAC0,     (uint32_t)mac[0] | ((uint32_t)mac[1] << 8) |
                       ((uint32_t)mac[2] << 16) | ((uint32_t)mac[3] << 24));
    w32(r, R_MAC0 + 4, (uint32_t)mac[4] | ((uint32_t)mac[5] << 8));
    w16(r, R_CPCR, r16(r, R_CPCR));
    w32(r, R_RDSAR,     (uint32_t)(r->rx_phys & 0xFFFFFFFFu));
    w32(r, R_RDSAR + 4, (uint32_t)(r->rx_phys >> 32));
    w32(r, R_TNPDS,     (uint32_t)(r->tx_phys & 0xFFFFFFFFu));
    w32(r, R_TNPDS + 4, (uint32_t)(r->tx_phys >> 32));
    w8 (r, R_MTPS, 0x3B);
    w16(r, R_RMS, RTL_BUFSZ);
    w32(r, R_TCR, 0x03000700u);
    w32(r, R_RCR, (7u << 13) | (7u << 8) | 0x0E);
    w8(r, R_CR, CR_TE | CR_RE);
    w8(r, R_9346CR, C9346_LOCK);

    r->ndev = netdev_register(mac, 1500, rtl_transmit, r);
    if (!r->ndev) { free(r); return -1; }
    r->ndev->link_up = (r8(r, R_PHYSTS) & PHYSTS_LINK) ? 1 : 0;

    r->next = g_nics;
    g_nics = r;

    int vec = irq_alloc_vector();
    if (vec > 0 && dev->cap_msi_off &&
        pci_enable_msi(dev, (uint8_t)vec, lapic_get_id()) == 0 &&
        irq_request(vec, rtl_irq, r, "rtl8169") == 0) {
        r->vector = vec;
        w16(r, R_ISR, 0xFFFF);
        w16(r, R_IMR, ISR_ROK | ISR_TOK | ISR_RER | ISR_RXOVW | ISR_FOVW | ISR_LINKCHG);
    } else {
        if (vec > 0) irq_free_vector(vec);
        r->polled = 1;
        w16(r, R_IMR, 0);
    }

    serial_printf("[rtl8169] %s: dev=%04x MAC %02x:%02x:%02x:%02x:%02x:%02x link=%s irq=%s\n",
                  r->ndev->name, dev->device_id,
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  r->ndev->link_up ? "up" : "down",
                  r->polled ? "poll" : "msi");
    return 0;
}

static const pci_driver_t g_rtl8169_driver = {
    .name           = "rtl8169",
    .match_vendor   = 0x10EC,
    .match_device   = -1,
    .match_class    = 0x02,
    .match_subclass = 0x00,
    .probe          = rtl_probe,
};

void rtl8169_init(void) {
    pci_register_driver(&g_rtl8169_driver);
}

void rtl8169_start_worker(void) {
    for (rtl8169_t *r = g_nics; r; r = r->next)
        if (r->polled) {
            task_create("rtl8169_worker", rtl_worker, NULL, 1);
            return;
        }
}
