#include "../../../include/drivers/net/rtl8139.h"
#include "../../../include/net/netdev.h"
#include "../../../include/drivers/pci.h"
#include "../../../include/memory/dma.h"
#include "../../../include/sched/sched.h"
#include "../../../include/sched/spinlock.h"
#include "../../../include/io/serial.h"
#include <string.h>
#include <stdlib.h>

#define REG_IDR0     0x00
#define REG_TSD0     0x10
#define REG_TSAD0    0x20
#define REG_RBSTART  0x30
#define REG_CR       0x37
#define REG_CAPR     0x38
#define REG_IMR      0x3C
#define REG_ISR      0x3E
#define REG_TCR      0x40
#define REG_RCR      0x44
#define REG_CONFIG1  0x52

#define CR_RST       0x10
#define CR_RE        0x08
#define CR_TE        0x04
#define CR_BUFE      0x01

#define RCR_WRAP     (1u << 7)
#define RCR_AB       (1u << 3)
#define RCR_AM       (1u << 2)
#define RCR_APM      (1u << 1)
#define RCR_AAP      (1u << 0)

#define ISR_ROK      0x0001
#define ISR_TOK      0x0004

#define RX_BUF_LEN   8192
#define RX_BUF_ALLOC (RX_BUF_LEN + 16 + 1536)
#define TX_BUFS      4
#define TX_BUFSZ     2048

typedef struct rtl {
    volatile uint8_t *regs;
    netdev_t         *ndev;

    uint8_t          *rx;      uintptr_t rx_phys;
    uint32_t          rx_off;

    uint8_t          *tx[TX_BUFS]; uintptr_t tx_phys[TX_BUFS];
    int               tx_cur;

    spinlock_t        lock;
    struct rtl       *next;
} rtl_t;

static rtl_t *g_rtls;

static inline uint8_t  rr8 (rtl_t *r, uint32_t o) { return *(volatile uint8_t  *)(r->regs + o); }
static inline uint32_t rr32(rtl_t *r, uint32_t o) { return *(volatile uint32_t *)(r->regs + o); }
static inline void rw8 (rtl_t *r, uint32_t o, uint8_t v)  { *(volatile uint8_t  *)(r->regs + o) = v; }
static inline void rw16(rtl_t *r, uint32_t o, uint16_t v) { *(volatile uint16_t *)(r->regs + o) = v; }
static inline void rw32(rtl_t *r, uint32_t o, uint32_t v) { *(volatile uint32_t *)(r->regs + o) = v; }

static int rtl_transmit(netdev_t *nd, const void *frame, size_t len) {
    rtl_t *r = nd->priv;
    if (len == 0 || len > TX_BUFSZ) return -1;

    spinlock_acquire(&r->lock);
    int i = r->tx_cur;
    memcpy(r->tx[i], frame, len);
    if (len < 60) { memset(r->tx[i] + len, 0, 60 - len); len = 60; }
    rw32(r, REG_TSAD0 + i * 4, (uint32_t)r->tx_phys[i]);
    rw32(r, REG_TSD0 + i * 4, (uint32_t)len);
    r->tx_cur = (i + 1) % TX_BUFS;
    spinlock_release(&r->lock);

    for (int t = 0; t < 1000000; t++)
        if (rr32(r, REG_TSD0 + i * 4) & 0x8000) break;
    return 0;
}

static void rtl_rx_drain(rtl_t *r) {
    uint8_t buf[2048];
    while (!(rr8(r, REG_CR) & CR_BUFE)) {
        spinlock_acquire(&r->lock);
        uint32_t off = r->rx_off;
        uint16_t status = (uint16_t)(r->rx[off] | (r->rx[off + 1] << 8));
        uint16_t len    = (uint16_t)(r->rx[off + 2] | (r->rx[off + 3] << 8));
        if (!(status & 0x0001) || len < 4 || len > 1600) {
            r->rx_off = 0;
            rw16(r, REG_CAPR, (uint16_t)(0 - 16));
            spinlock_release(&r->lock);
            break;
        }
        uint16_t plen = (uint16_t)(len - 4);
        for (uint16_t k = 0; k < plen && k < sizeof(buf); k++)
            buf[k] = r->rx[(off + 4 + k) % RX_BUF_LEN];
        r->rx_off = (off + len + 4 + 3) & ~3u;
        r->rx_off %= RX_BUF_LEN;
        rw16(r, REG_CAPR, (uint16_t)(r->rx_off - 16));
        spinlock_release(&r->lock);

        if (plen >= 14) net_rx(r->ndev, buf, plen);
    }
    rw16(r, REG_ISR, ISR_ROK | ISR_TOK);
}

static void rtl_worker(void *arg) {
    (void)arg;
    for (;;) {
        for (rtl_t *r = g_rtls; r; r = r->next) rtl_rx_drain(r);
        task_sleep_ms(1);
    }
}

static int rtl_probe(pci_device_t *dev) {
    int bar = (dev->bars[1].type == PCI_BAR_TYPE_MEM && dev->bars[1].base) ? 1 :
              ((dev->bars[0].type == PCI_BAR_TYPE_MEM && dev->bars[0].base) ? 0 : -1);
    if (bar < 0) { serial_printf("[rtl8139] no MMIO BAR\n"); return -1; }

    uint16_t cmd = pci_config_read16(dev->segment, dev->bus, dev->device, dev->function, PCI_COMMAND);
    cmd |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
    pci_config_write16(dev->segment, dev->bus, dev->device, dev->function, PCI_COMMAND, cmd);

    rtl_t *r = calloc(1, sizeof(*r));
    if (!r) return -1;
    r->regs = (volatile uint8_t *)mmio_map(dev->bars[bar].base, dev->bars[bar].size);
    if (!r->regs) { free(r); return -1; }

    rw8(r, REG_CONFIG1, 0x00);
    rw8(r, REG_CR, CR_RST);
    for (int i = 0; i < 1000000; i++) if (!(rr8(r, REG_CR) & CR_RST)) break;

    r->rx = dma_alloc_coherent_low(RX_BUF_ALLOC, &r->rx_phys);
    if (!r->rx) { free(r); return -1; }
    memset(r->rx, 0, RX_BUF_ALLOC);
    for (int i = 0; i < TX_BUFS; i++)
        r->tx[i] = dma_alloc_coherent_low(TX_BUFSZ, &r->tx_phys[i]);

    rw32(r, REG_RBSTART, (uint32_t)r->rx_phys);
    rw16(r, REG_IMR, ISR_ROK | ISR_TOK);
    rw32(r, REG_RCR, RCR_WRAP | RCR_AB | RCR_AM | RCR_APM | RCR_AAP | (7u << 8) | (7u << 13));
    rw32(r, REG_TCR, (7u << 8) | (3u << 24));
    rw8(r, REG_CR, CR_RE | CR_TE);

    uint8_t mac[6];
    for (int i = 0; i < 6; i++) mac[i] = rr8(r, REG_IDR0 + i);

    r->ndev = netdev_register(mac, 1500, rtl_transmit, r);
    if (!r->ndev) { free(r); return -1; }
    r->ndev->link_up = 1;

    r->next = g_rtls;
    g_rtls = r;

    serial_printf("[rtl8139] %s: MAC %02x:%02x:%02x:%02x:%02x:%02x link=up irq=poll\n",
                  r->ndev->name, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return 0;
}

static const pci_driver_t g_rtl_driver = {
    .name           = "rtl8139",
    .match_vendor   = 0x10EC,
    .match_device   = 0x8139,
    .match_class    = -1,
    .match_subclass = -1,
    .probe          = rtl_probe,
};

void rtl8139_init(void) {
    pci_register_driver(&g_rtl_driver);
}

void rtl8139_start_worker(void) {
    if (g_rtls) task_create("rtl8139_worker", rtl_worker, NULL, 1);
}
