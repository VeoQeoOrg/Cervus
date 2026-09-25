#include "../../../include/drivers/net/rtl8169.h"
#include "../../../include/net/netdev.h"
#include "../../../include/drivers/pci.h"
#include "../../../include/drivers/timer.h"
#include "../../../include/memory/dma.h"
#include "../../../include/apic/apic.h"
#include "../../../include/interrupts/irq.h"
#include "../../../include/sched/sched.h"
#include "../../../include/sched/spinlock.h"
#include "../../../include/io/serial.h"
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
#define R_CONFIG2   0x53
#define R_CONFIG5   0x56
#define R_PHYSTS    0x6C
#define R_PMCH      0x6F
#define R_ERIDR     0x70
#define R_ERIAR     0x74
#define R_EPHYAR    0x80
#define R_OCPDR     0xB0
#define R_GPHY_OCP  0xB8
#define R_RMS       0xDA
#define R_CPCR      0xE0
#define R_INTRMIT   0xE2
#define R_RDSAR     0xE4
#define R_MTPS      0xEC
#define R_DLLPR     0xD0
#define R_MISC      0xF0
#define R_MISC1     0xF2

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

#define PHYSTS_FULLDUP 0x01
#define PHYSTS_LINK    0x02
#define PHYSTS_10M     0x04
#define PHYSTS_100M    0x08
#define PHYSTS_1000M   0x10

#define CONFIG2_CLKREQ_EN 0x80
#define CONFIG5_ASPM_EN   0x01
#define MISC_RXDV_GATED   (1u << 19)

#define OCP_FLAG          0x80000000u
#define OCP_STD_PHY_BASE  0xA400
#define ERIAR_FLAG        0x80000000u
#define ERIAR_MASK_1111   (0xFu << 12)
#define ERIAR_MASK_0011   (0x3u << 12)
#define ERIAR_MASK_0001   (0x1u << 12)
#define EPHYAR_FLAG       0x80000000u

#define MII_BMCR          0x00
#define MII_BMSR          0x01
#define BMCR_RESET        0x8000
#define BMCR_ANENABLE     0x1000
#define BMCR_PDOWN        0x0800
#define BMCR_ISOLATE      0x0400
#define BMCR_ANRESTART    0x0200

#define DESC_OWN      0x80000000u
#define DESC_EOR      0x40000000u
#define DESC_FS       0x20000000u
#define DESC_LS       0x10000000u
#define DESC_LEN_MASK 0x00003FFFu

#define RTL_NUM_RX  32
#define RTL_NUM_TX  16
#define RTL_BUFSZ   2048

enum { RTL_CLASSIC, RTL_8168_MODERN, RTL_8125 };

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

    uint16_t          xid;
    int               family;
    int               vector;
    int               polled;
    spinlock_t        lock;

    struct rtl8169   *next;
} rtl8169_t;

static rtl8169_t *g_nics;

static inline uint8_t  r8 (rtl8169_t *r, uint32_t o) { return *(volatile uint8_t  *)(r->regs + o); }
static inline uint16_t r16(rtl8169_t *r, uint32_t o) { return *(volatile uint16_t *)(r->regs + o); }
static inline uint32_t r32(rtl8169_t *r, uint32_t o) { return *(volatile uint32_t *)(r->regs + o); }
static inline void w8 (rtl8169_t *r, uint32_t o, uint8_t  v) { *(volatile uint8_t  *)(r->regs + o) = v; }
static inline void w16(rtl8169_t *r, uint32_t o, uint16_t v) { *(volatile uint16_t *)(r->regs + o) = v; }
static inline void w32(rtl8169_t *r, uint32_t o, uint32_t v) { *(volatile uint32_t *)(r->regs + o) = v; }

static void rtl_udelay(uint32_t us) {
    uint64_t end = sched_now_ns() + (uint64_t)us * 1000ULL;
    while (sched_now_ns() < end) __asm__ volatile ("pause");
}

static int rtl_wait32(rtl8169_t *r, uint32_t reg, uint32_t mask, int want_set, int tries, uint32_t us) {
    for (int i = 0; i < tries; i++) {
        uint32_t v = r32(r, reg) & mask;
        if (want_set ? v != 0 : v == 0) return 0;
        rtl_udelay(us);
    }
    return -1;
}

static int phy_ocp_read(rtl8169_t *r, uint32_t reg) {
    w32(r, R_GPHY_OCP, reg << 15);
    if (rtl_wait32(r, R_GPHY_OCP, OCP_FLAG, 1, 25, 10) < 0) return -1;
    return (int)(r32(r, R_GPHY_OCP) & 0xFFFF);
}

static void phy_ocp_write(rtl8169_t *r, uint32_t reg, uint16_t val) {
    w32(r, R_GPHY_OCP, OCP_FLAG | (reg << 15) | val);
    rtl_wait32(r, R_GPHY_OCP, OCP_FLAG, 0, 25, 10);
}

static int mii_read(rtl8169_t *r, int reg) {
    return phy_ocp_read(r, OCP_STD_PHY_BASE + (uint32_t)reg * 2);
}

static void mii_write(rtl8169_t *r, int reg, uint16_t val) {
    phy_ocp_write(r, OCP_STD_PHY_BASE + (uint32_t)reg * 2, val);
}

static uint16_t mac_ocp_read(rtl8169_t *r, uint32_t reg) {
    w32(r, R_OCPDR, (reg >> 1) << 15);
    return (uint16_t)r32(r, R_OCPDR);
}

static void mac_ocp_write(rtl8169_t *r, uint32_t reg, uint16_t val) {
    w32(r, R_OCPDR, OCP_FLAG | ((reg >> 1) << 15) | val);
}

static void mac_ocp_modify(rtl8169_t *r, uint32_t reg, uint16_t clear, uint16_t set) {
    uint16_t v = mac_ocp_read(r, reg);
    mac_ocp_write(r, reg, (uint16_t)((v & ~clear) | set));
}

static uint32_t eri_read(rtl8169_t *r, uint32_t addr) {
    w32(r, R_ERIAR, ERIAR_MASK_1111 | addr);
    if (rtl_wait32(r, R_ERIAR, ERIAR_FLAG, 1, 100, 100) < 0) return 0xFFFFFFFFu;
    return r32(r, R_ERIDR);
}

static void eri_write(rtl8169_t *r, uint32_t addr, uint32_t mask, uint32_t val) {
    w32(r, R_ERIDR, val);
    w32(r, R_ERIAR, ERIAR_FLAG | mask | addr);
    rtl_wait32(r, R_ERIAR, ERIAR_FLAG, 0, 100, 100);
}

static uint16_t ephy_read(rtl8169_t *r, int reg) {
    w32(r, R_EPHYAR, ((uint32_t)reg & 0x1F) << 16);
    if (rtl_wait32(r, R_EPHYAR, EPHYAR_FLAG, 1, 10, 100) < 0) return 0xFFFF;
    return (uint16_t)(r32(r, R_EPHYAR) & 0xFFFF);
}

static void ephy_write(rtl8169_t *r, int reg, uint16_t val) {
    w32(r, R_EPHYAR, EPHYAR_FLAG | val | (((uint32_t)reg & 0x1F) << 16));
    rtl_wait32(r, R_EPHYAR, EPHYAR_FLAG, 0, 10, 100);
    rtl_udelay(10);
}

static void ephy_modify(rtl8169_t *r, int reg, uint16_t clear, uint16_t set) {
    uint16_t v = ephy_read(r, reg);
    ephy_write(r, reg, (uint16_t)((v & ~clear) | set));
}

static int classify(uint16_t xid) {
    uint16_t fam = xid & 0x7C0;
    if (fam >= 0x600 && xid != 0x6C0) return RTL_8125;
    if (fam >= 0x4C0 || xid == 0x6C0) return RTL_8168_MODERN;
    return RTL_CLASSIC;
}

static int is_8168h(uint16_t xid) {
    uint16_t v = xid & 0x7CF;
    return v == 0x540 || v == 0x541 || v == 0x6C0;
}

static void rtl_link_update(rtl8169_t *r) {
    uint8_t st = r8(r, R_PHYSTS);
    int up = (st & PHYSTS_LINK) != 0;
    if (!r->ndev || up == r->ndev->link_up) return;
    r->ndev->link_up = up;
    if (up) {
        const char *speed = (st & PHYSTS_1000M) ? "1000" : (st & PHYSTS_100M) ? "100" : "10";
        serial_printf("[rtl8169] %s: link up, %s Mbps %s-duplex\n", r->ndev->name, speed,
                      (st & PHYSTS_FULLDUP) ? "full" : "half");
    } else {
        serial_printf("[rtl8169] %s: link down\n", r->ndev->name);
    }
}

static void rtl_rx_drain(rtl8169_t *r) {
    uint8_t buf[RTL_BUFSZ];
    for (;;) {
        uint64_t f = spinlock_acquire_irqsave(&r->lock);
        uint32_t i = r->rx_cur;
        uint32_t opts1 = r->rx[i].opts1;
        if (opts1 & DESC_OWN) { spinlock_release_irqrestore(&r->lock, f); return; }
        uint32_t len = opts1 & DESC_LEN_MASK;
        if (len >= 4) len -= 4;
        if (len > RTL_BUFSZ) len = RTL_BUFSZ;
        memcpy(buf, r->rx_buf[i], len);
        uint32_t eor = (i == RTL_NUM_RX - 1) ? DESC_EOR : 0;
        r->rx[i].opts1 = DESC_OWN | eor | RTL_BUFSZ;
        r->rx_cur = (i + 1) % RTL_NUM_RX;
        spinlock_release_irqrestore(&r->lock, f);

        net_rx(r->ndev, buf, len);
    }
}

static int rtl_transmit(netdev_t *nd, const void *frame, size_t len) {
    rtl8169_t *r = nd->priv;
    if (len == 0 || len > RTL_BUFSZ) return -1;

    uint64_t f = spinlock_acquire_irqsave(&r->lock);
    uint32_t i = r->tx_cur;
    if (r->tx[i].opts1 & DESC_OWN) { spinlock_release_irqrestore(&r->lock, f); return -1; }
    memcpy(r->tx_buf[i], frame, len);
    if (len < 60) {
        memset(r->tx_buf[i] + len, 0, 60 - len);
        len = 60;
    }
    uint32_t eor = (i == RTL_NUM_TX - 1) ? DESC_EOR : 0;
    r->tx[i].addr  = r->tx_buf_phys[i];
    r->tx[i].opts2 = 0;
    r->tx[i].opts1 = DESC_OWN | DESC_FS | DESC_LS | eor | (uint32_t)len;
    r->tx_cur = (i + 1) % RTL_NUM_TX;
    w8(r, R_TPPOLL, TPPOLL_NPQ);
    spinlock_release_irqrestore(&r->lock, f);

    for (int t = 0; t < 1000000; t++)
        if (!(r->tx[i].opts1 & DESC_OWN)) break;
    return 0;
}

static void rtl_irq(void *ctx) {
    rtl8169_t *r = ctx;
    uint16_t st = r16(r, R_ISR);
    w16(r, R_ISR, st);
    if (st & ISR_LINKCHG) rtl_link_update(r);
    rtl_rx_drain(r);
}

static void rtl_worker(void *arg) {
    (void)arg;
    uint32_t tick = 0;
    for (;;) {
        for (rtl8169_t *r = g_nics; r; r = r->next) {
            if (r->polled) rtl_rx_drain(r);
            if ((tick % 250) == 0) rtl_link_update(r);
        }
        tick++;
        task_sleep_ms(1);
    }
}

static void rtl_read_mac(rtl8169_t *r, uint8_t mac[6]) {
    for (int i = 0; i < 6; i++) mac[i] = r8(r, R_MAC0 + i);
    int zero = 1, ones = 1;
    for (int i = 0; i < 6; i++) { if (mac[i]) zero = 0; if (mac[i] != 0xFF) ones = 0; }
    if ((zero || ones) && r->family == RTL_8168_MODERN) {
        uint32_t lo = eri_read(r, 0xE0);
        uint32_t hi = eri_read(r, 0xE4);
        mac[0] = (uint8_t)lo; mac[1] = (uint8_t)(lo >> 8);
        mac[2] = (uint8_t)(lo >> 16); mac[3] = (uint8_t)(lo >> 24);
        mac[4] = (uint8_t)hi; mac[5] = (uint8_t)(hi >> 8);
        serial_printf("[rtl8169] MAC0 empty, took the address from ERI\n");
    }
}

static void rtl_8168h_ephy(rtl8169_t *r) {
    ephy_modify(r, 0x1E, 0x0800, 0x0001);
    ephy_modify(r, 0x1D, 0x0000, 0x0800);
    ephy_modify(r, 0x05, 0xFFFF, 0x2089);
    ephy_modify(r, 0x06, 0xFFFF, 0x5881);
    ephy_modify(r, 0x04, 0xFFFF, 0x854A);
    ephy_modify(r, 0x01, 0xFFFF, 0x068B);
}

static void rtl_modern_mac_setup(rtl8169_t *r) {
    if (is_8168h(r->xid)) rtl_8168h_ephy(r);

    eri_write(r, 0xC8, ERIAR_MASK_1111, 0x00080002);
    eri_write(r, 0xE8, ERIAR_MASK_1111, 0x00100006);
    eri_write(r, 0xCC, ERIAR_MASK_0001, 0x38);
    eri_write(r, 0xD0, ERIAR_MASK_0001, 0x48);

    eri_write(r, 0xDC, ERIAR_MASK_1111, eri_read(r, 0xDC) & ~1u);
    eri_write(r, 0xDC, ERIAR_MASK_1111, eri_read(r, 0xDC) | 1u);
    eri_write(r, 0xDC, ERIAR_MASK_1111, eri_read(r, 0xDC) | 0x1Cu);
    eri_write(r, 0x5F0, ERIAR_MASK_0011, 0x4F87);

    w32(r, R_MISC, r32(r, R_MISC) & ~MISC_RXDV_GATED);

    eri_write(r, 0xC0, ERIAR_MASK_0011, 0x0000);
    eri_write(r, 0xB8, ERIAR_MASK_0011, 0x0000);

    w8(r, R_DLLPR, r8(r, R_DLLPR) & ~0xC0);
    w8(r, R_MISC1, r8(r, R_MISC1) & ~0x40);

    if (is_8168h(r->xid)) {
        mac_ocp_modify(r, 0xE056, 0x00F0, 0x0070);
        mac_ocp_modify(r, 0xE052, 0x6000, 0x8008);
        mac_ocp_modify(r, 0xE0D6, 0x01FF, 0x017F);
        mac_ocp_modify(r, 0xD420, 0x0FFF, 0x047F);
        mac_ocp_write(r, 0xE63E, 0x0001);
        mac_ocp_write(r, 0xE63E, 0x0000);
        mac_ocp_write(r, 0xC094, 0x0000);
        mac_ocp_write(r, 0xC09E, 0x0000);
    }
}

static void rtl_modern_phy_up(rtl8169_t *r) {
    int bmcr = mii_read(r, MII_BMCR);
    int bmsr = mii_read(r, MII_BMSR);
    serial_printf("[rtl8169] PHY BMCR=%04x BMSR=%04x\n", bmcr & 0xFFFF, bmsr & 0xFFFF);
    if (bmcr < 0) return;
    uint16_t v = (uint16_t)bmcr;
    v &= (uint16_t)~(BMCR_PDOWN | BMCR_ISOLATE);
    v |= BMCR_ANENABLE | BMCR_ANRESTART;
    mii_write(r, MII_BMCR, v);
}

static int rtl_probe(pci_device_t *dev) {
    if (dev->device_id == 0x8139 || dev->device_id == 0x8138) return -1;

    int barx = -1;
    for (int i = 0; i < 6; i++)
        if (dev->bars[i].type == PCI_BAR_TYPE_MEM && dev->bars[i].base) { barx = i; break; }
    if (barx < 0) { serial_printf("[rtl8169] %04x: no MMIO BAR\n", dev->device_id); return -1; }

    pci_power_up(dev);
    uint16_t cmd = pci_config_read16(dev->segment, dev->bus, dev->device, dev->function, PCI_COMMAND);
    cmd |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
    pci_config_write16(dev->segment, dev->bus, dev->device, dev->function, PCI_COMMAND, cmd);

    rtl8169_t *r = calloc(1, sizeof(*r));
    if (!r) return -1;
    r->regs = (volatile uint8_t *)mmio_map(dev->bars[barx].base, dev->bars[barx].size);
    if (!r->regs) { free(r); return -1; }

    r->xid = (uint16_t)((r32(r, R_TCR) >> 20) & 0xFCF);
    r->family = classify(r->xid);
    serial_printf("[rtl8169] %04x:%04x chip xid %03x (%s)\n", dev->vendor_id, dev->device_id, r->xid,
                  r->family == RTL_8168_MODERN ? (is_8168h(r->xid) ? "RTL8111H/8168H" : "RTL8168G family")
                  : r->family == RTL_8125 ? "RTL8125, not supported" : "RTL8169/8168 classic");
    if (r->family == RTL_8125) { free(r); return -1; }

    if (r->family == RTL_8168_MODERN) {
        pci_disable_aspm(dev);
        w8(r, R_PMCH, r8(r, R_PMCH) | 0xC0);
        rtl_udelay(100);
    }

    w8(r, R_CR, CR_RST);
    for (int i = 0; i < 1000; i++) {
        if (!(r8(r, R_CR) & CR_RST)) break;
        rtl_udelay(100);
    }

    uint8_t mac[6];
    rtl_read_mac(r, mac);

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
    if (r->family == RTL_8168_MODERN) {
        w8(r, R_CONFIG2, r8(r, R_CONFIG2) & ~CONFIG2_CLKREQ_EN);
        w8(r, R_CONFIG5, r8(r, R_CONFIG5) & ~CONFIG5_ASPM_EN);
        rtl_modern_mac_setup(r);
    }
    w32(r, R_MAC0,     (uint32_t)mac[0] | ((uint32_t)mac[1] << 8) |
                       ((uint32_t)mac[2] << 16) | ((uint32_t)mac[3] << 24));
    w32(r, R_MAC0 + 4, (uint32_t)mac[4] | ((uint32_t)mac[5] << 8));
    w16(r, R_CPCR, r16(r, R_CPCR));
    w16(r, R_INTRMIT, 0);
    w32(r, R_RDSAR,     (uint32_t)(r->rx_phys & 0xFFFFFFFFu));
    w32(r, R_RDSAR + 4, (uint32_t)(r->rx_phys >> 32));
    w32(r, R_TNPDS,     (uint32_t)(r->tx_phys & 0xFFFFFFFFu));
    w32(r, R_TNPDS + 4, (uint32_t)(r->tx_phys >> 32));
    w8 (r, R_MTPS, r->family == RTL_8168_MODERN ? 0x27 : 0x3B);
    w16(r, R_RMS, RTL_BUFSZ);
    w8(r, R_CR, CR_TE | CR_RE);
    if (r->family == RTL_8168_MODERN) {
        w32(r, R_RCR, (1u << 15) | (1u << 14) | (1u << 11) | (7u << 8) | 0x0E);
        w32(r, R_TCR, 0x03000700u | 0x80u);
    } else {
        w32(r, R_TCR, 0x03000700u);
        w32(r, R_RCR, (7u << 13) | (7u << 8) | 0x0E);
    }
    w32(r, R_MAR0,     0xFFFFFFFFu);
    w32(r, R_MAR0 + 4, 0xFFFFFFFFu);
    w8(r, R_9346CR, C9346_LOCK);

    if (r->family == RTL_8168_MODERN) rtl_modern_phy_up(r);

    r->ndev = netdev_register(mac, 1500, rtl_transmit, r);
    if (!r->ndev) { free(r); return -1; }
    r->ndev->link_up = 0;
    rtl_link_update(r);

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

static const uint32_t g_rtl8169_ids[] = {
    0x10EC8129u, 0x10EC8136u, 0x10EC8161u, 0x10EC8167u,
    0x10EC8168u, 0x10EC8169u, 0x10EC2502u, 0x10EC2600u,
    0x11864300u, 0x11864302u, 0x1259C107u, 0x17371032u,
};

static const pci_driver_t g_rtl8169_driver = {
    .name           = "rtl8169",
    .match_vendor   = -1,
    .match_device   = -1,
    .match_class    = -1,
    .match_subclass = -1,
    .match_ids      = g_rtl8169_ids,
    .match_id_count = (int)(sizeof g_rtl8169_ids / sizeof g_rtl8169_ids[0]),
    .probe          = rtl_probe,
};

void rtl8169_init(void) {
    pci_register_driver(&g_rtl8169_driver);
}

void rtl8169_start_worker(void) {
    if (g_nics) task_create("rtl8169_worker", rtl_worker, NULL, 1);
}
