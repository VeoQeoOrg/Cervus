#include "../../../include/drivers/net/atl1c.h"
#include "../../../include/net/netdev.h"
#include "../../../include/drivers/pci.h"
#include "../../../include/memory/dma.h"
#include "../../../include/apic/apic.h"
#include "../../../include/interrupts/irq.h"
#include "../../../include/sched/sched.h"
#include "../../../include/sched/spinlock.h"
#include "../../../include/io/serial.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#define ATL_MASTER_CTRL         0x1400
#define ATL_GPHY_CTRL           0x140C
#define ATL_IDLE_STATUS         0x1410
#define ATL_CLK_GATING_CTRL     0x1814
#define ATL_MDIO_CTRL           0x1414
#define ATL_MAC_CTRL            0x1480
#define ATL_MAC_IPG_IFG         0x1484
#define ATL_MAC_STA_ADDR        0x1488
#define ATL_RX_HASH_TABLE       0x1490
#define ATL_MAC_HALF_DUPLX_CTRL 0x1498
#define ATL_MTU                 0x149C
#define ATL_RX_BASE_ADDR_HI     0x1540
#define ATL_TX_BASE_ADDR_HI     0x1544
#define ATL_RFD0_HEAD_ADDR_LO   0x1550
#define ATL_RFD_RING_SIZE       0x1560
#define ATL_RX_BUF_SIZE         0x1564
#define ATL_RRD0_HEAD_ADDR_LO   0x1568
#define ATL_RRD_RING_SIZE       0x1578
#define ATL_TPD_PRI0_ADDR_LO    0x1580
#define ATL_TPD_RING_SIZE       0x1584
#define ATL_TXQ_CTRL            0x1590
#define ATL_RXQ_CTRL            0x15A0
#define ATL_RFD_FREE_THRESH     0x15A4
#define ATL_DMA_CTRL            0x15C0
#define ATL_MB_RFD0_PROD_IDX    0x15E0
#define ATL_TPD_PRI0_PIDX       0x15F2
#define ATL_TPD_PRI0_CIDX       0x15F6
#define ATL_ISR                 0x1600
#define ATL_IMR                 0x1604

#define MASTER_SOFT_RST         (1u << 0)
#define MASTER_MTIMER_EN        (1u << 8)
#define MASTER_ITIMER_EN        (1u << 11)
#define MASTER_INT_RDCLR        (1u << 14)

#define IDLE_BUSY_MASK          0x0Fu

#define GPHY_EXT_RESET          (1u << 0)
#define GPHY_LED_MODE           (1u << 2)
#define GPHY_ANEG_NOW           (1u << 3)
#define GPHY_GATE_25M_EN        (1u << 5)
#define GPHY_LPW_EXIT           (1u << 6)
#define GPHY_PHY_IDDQ_DIS       (1u << 8)
#define GPHY_HIB_EN             (1u << 10)
#define GPHY_HIB_PULSE          (1u << 11)
#define GPHY_SEL_ANA_RST        (1u << 12)
#define GPHY_PHY_PLL_ON         (1u << 13)
#define GPHY_10AB_EN            (1u << 16)
#define GPHY_100AB_EN           (1u << 17)

#define MDIO_DATA_MASK          0xFFFFu
#define MDIO_REG_SHIFT          16
#define MDIO_OP_READ            (1u << 21)
#define MDIO_SPRES_PRMBL        (1u << 22)
#define MDIO_START              (1u << 23)
#define MDIO_CLK_SHIFT          24
#define MDIO_CLK_25_4           0u
#define MDIO_BUSY               (1u << 27)

#define MII_BMCR                0x00
#define MII_BMSR                0x01
#define MII_PSSR                0x11
#define BMCR_RESET              0x8000
#define BMCR_ANENABLE           0x1000
#define BMCR_ANRESTART          0x0200
#define BMSR_LSTATUS            0x0004
#define PSSR_RESOLVED           0x0800
#define PSSR_DPLX               0x2000
#define PSSR_SPEED_MASK         0xC000
#define PSSR_10MBS              0x0000
#define PSSR_100MBS             0x4000
#define PSSR_1000MBS            0x8000

#define MAC_TX_EN               (1u << 0)
#define MAC_RX_EN               (1u << 1)
#define MAC_DUPLX               (1u << 5)
#define MAC_ADD_CRC             (1u << 6)
#define MAC_PAD                 (1u << 7)
#define MAC_PRMLEN_SHIFT        10
#define MAC_PROMIS_EN           (1u << 15)
#define MAC_SPEED_SHIFT         20
#define MAC_SPEED_10_100        1u
#define MAC_SPEED_1000          2u
#define MAC_MC_ALL_EN           (1u << 25)
#define MAC_BC_EN               (1u << 26)

#define TXQ_CTRL_EN             (1u << 5)
#define TXQ_CTRL_ENH_MODE       (1u << 6)
#define TXQ_TPD_BURST_SHIFT     0
#define TXQ_TXF_BURST_SHIFT     16
#define TXQ_TXF_BURST_PREF      0x200u
#define TXQ_TPD_BURST_DEF       5u

#define RXQ_RFD_BURST_SHIFT     20
#define RXQ_RFD_BURST_DEF       8u
#define RXQ_CTRL_EN             (1u << 31)

#define TPD_EOP                 (1u << 31)

#define RRS_RFD_CNT_SHIFT       16
#define RRS_RFD_CNT_MASK        0x0Fu
#define RRS_RFD_INDEX_SHIFT     20
#define RRS_RFD_INDEX_MASK      0x0FFFu
#define RRS_PKT_SIZE_MASK       0x3FFFu
#define RRS_RX_ERR_SUM          (1u << 20)
#define RRS_RXD_UPDATED         (1u << 31)
#define ATL_ISR_DIS_INT         0x80000000u

#define CLK_GATING_DMAW_EN      0x0001u
#define CLK_GATING_DMAR_EN      0x0002u
#define CLK_GATING_TXQ_EN       0x0004u
#define CLK_GATING_RXQ_EN       0x0008u
#define CLK_GATING_TXMAC_EN     0x0010u
#define CLK_GATING_RXMAC_EN     0x0020u
#define CLK_GATING_EN_ALL       (CLK_GATING_DMAW_EN | CLK_GATING_DMAR_EN | \
                                 CLK_GATING_TXQ_EN  | CLK_GATING_RXQ_EN  | \
                                 CLK_GATING_TXMAC_EN | CLK_GATING_RXMAC_EN)

#define DMA_RORDER_MODE_OUT     4u
#define DMA_RREQ_PRI_DATA       (1u << 10)
#define DMA_RREQ_BLEN_SHIFT     4
#define DMA_WREQ_BLEN_SHIFT     7
#define DMA_REQ_BLEN_128        0u
#define DMA_RDLY_CNT_SHIFT      11
#define DMA_RDLY_CNT_DEF        15u
#define DMA_WDLY_CNT_SHIFT      16
#define DMA_WDLY_CNT_DEF        4u

#define IDLE_STATUS_TXQ_BUSY    (1u << 3)
#define IDLE_STATUS_RXQ_BUSY    (1u << 2)
#define IDLE_STATUS_TXMAC_BUSY  (1u << 1)
#define IDLE_STATUS_RXMAC_BUSY  (1u << 0)

#define ATL_NUM_RFD             64
#define ATL_NUM_TPD             64
#define ATL_BUFSZ               2048
#define ATL_ETH_FCS_LEN         4

typedef struct {
    uint64_t addr;
} __attribute__((packed)) atl_rfd_t;

typedef struct {
    uint32_t word0;
    uint32_t rss_hash;
    uint16_t vlan_tag;
    uint16_t flag;
    uint32_t word3;
} __attribute__((packed)) atl_rrd_t;

typedef struct {
    uint16_t buffer_len;
    uint16_t vlan_tag;
    uint32_t word1;
    uint64_t addr;
} __attribute__((packed)) atl_tpd_t;

typedef struct atl1c_dev {
    volatile uint8_t *regs;
    netdev_t         *ndev;
    spinlock_t        lock;

    atl_rfd_t *rfd;   uintptr_t rfd_phys;
    atl_rrd_t *rrd;   uintptr_t rrd_phys;
    atl_tpd_t *tpd;   uintptr_t tpd_phys;

    void     *rx_buf[ATL_NUM_RFD];
    uintptr_t rx_buf_phys[ATL_NUM_RFD];
    void     *tx_buf[ATL_NUM_TPD];
    uintptr_t tx_buf_phys[ATL_NUM_TPD];

    uint16_t rfd_prod;
    uint16_t rrd_next;
    uint16_t tpd_prod;

    int polled;
    int vector;

    struct atl1c_dev *next;
} atl1c_t;

static atl1c_t *g_nics;

static inline uint32_t ar32(atl1c_t *a, uint32_t r) {
    return *(volatile uint32_t *)(a->regs + r);
}
static inline void aw32(atl1c_t *a, uint32_t r, uint32_t v) {
    *(volatile uint32_t *)(a->regs + r) = v;
}
static inline uint16_t ar16(atl1c_t *a, uint32_t r) {
    return *(volatile uint16_t *)(a->regs + r);
}
static inline void aw16(atl1c_t *a, uint32_t r, uint16_t v) {
    *(volatile uint16_t *)(a->regs + r) = v;
}

static void atl_delay(uint32_t loops) {
    for (volatile uint32_t i = 0; i < loops * 200u; i++) { }
}

static int atl_mdio_read(atl1c_t *a, uint8_t reg, uint16_t *out) {
    aw32(a, ATL_MDIO_CTRL,
         MDIO_START | MDIO_SPRES_PRMBL | MDIO_OP_READ |
         (MDIO_CLK_25_4 << MDIO_CLK_SHIFT) |
         (((uint32_t)reg & 0x1Fu) << MDIO_REG_SHIFT));
    for (int i = 0; i < 200; i++) {
        atl_delay(2);
        uint32_t v = ar32(a, ATL_MDIO_CTRL);
        if (!(v & MDIO_BUSY)) { *out = (uint16_t)(v & MDIO_DATA_MASK); return 0; }
    }
    return -1;
}

static int atl_mdio_write(atl1c_t *a, uint8_t reg, uint16_t val) {
    aw32(a, ATL_MDIO_CTRL,
         MDIO_START | MDIO_SPRES_PRMBL |
         (MDIO_CLK_25_4 << MDIO_CLK_SHIFT) |
         (((uint32_t)reg & 0x1Fu) << MDIO_REG_SHIFT) | val);
    for (int i = 0; i < 200; i++) {
        atl_delay(2);
        if (!(ar32(a, ATL_MDIO_CTRL) & MDIO_BUSY)) return 0;
    }
    return -1;
}

static int atl_reset(atl1c_t *a) {
    aw32(a, ATL_IMR, 0);
    aw32(a, ATL_ISR, 0xFFFFFFFFu);

    aw32(a, ATL_MASTER_CTRL, MASTER_SOFT_RST);
    atl_delay(50);

    for (int i = 0; i < 100; i++) {
        if (!(ar32(a, ATL_IDLE_STATUS) & IDLE_BUSY_MASK)) return 0;
        atl_delay(20);
    }
    serial_printf("[atl1c] reset timeout, idle=0x%x\n", ar32(a, ATL_IDLE_STATUS));
    return -1;
}

static void atl_phy_init(atl1c_t *a) {
    uint32_t g = GPHY_SEL_ANA_RST | GPHY_HIB_PULSE | GPHY_HIB_EN |
                 GPHY_PHY_IDDQ_DIS | GPHY_PHY_PLL_ON | GPHY_LED_MODE |
                 GPHY_100AB_EN | GPHY_10AB_EN;
    aw32(a, ATL_GPHY_CTRL, g);
    atl_delay(10);
    aw32(a, ATL_GPHY_CTRL, g | GPHY_EXT_RESET);
    atl_delay(100);

    uint16_t bmcr = 0;
    if (atl_mdio_read(a, MII_BMCR, &bmcr) == 0) {
        atl_mdio_write(a, MII_BMCR, BMCR_RESET);
        atl_delay(100);
        atl_mdio_write(a, MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART);
    }
}

static void atl_read_mac(atl1c_t *a, uint8_t mac[6]) {
    uint32_t hi = ar32(a, ATL_MAC_STA_ADDR);
    uint32_t lo = ar32(a, ATL_MAC_STA_ADDR + 4);
    mac[0] = (uint8_t)(lo >> 8);
    mac[1] = (uint8_t)(lo);
    mac[2] = (uint8_t)(hi >> 24);
    mac[3] = (uint8_t)(hi >> 16);
    mac[4] = (uint8_t)(hi >> 8);
    mac[5] = (uint8_t)(hi);
}

static void atl_write_mac(atl1c_t *a, const uint8_t mac[6]) {
    aw32(a, ATL_MAC_STA_ADDR,
         ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) |
         ((uint32_t)mac[4] << 8)  | (uint32_t)mac[5]);
    aw32(a, ATL_MAC_STA_ADDR + 4,
         ((uint32_t)mac[0] << 8) | (uint32_t)mac[1]);
}

static int atl_link_state(atl1c_t *a, uint32_t *speed, int *duplex) {
    uint16_t bmsr = 0, pssr = 0;
    if (atl_mdio_read(a, MII_BMSR, &bmsr) != 0) return 0;
    if (atl_mdio_read(a, MII_BMSR, &bmsr) != 0) return 0;
    if (!(bmsr & BMSR_LSTATUS)) return 0;
    if (atl_mdio_read(a, MII_PSSR, &pssr) != 0) return 0;
    if (!(pssr & PSSR_RESOLVED)) return 0;
    *duplex = (pssr & PSSR_DPLX) ? 1 : 0;
    switch (pssr & PSSR_SPEED_MASK) {
        case PSSR_1000MBS: *speed = 1000; break;
        case PSSR_100MBS:  *speed = 100;  break;
        default:           *speed = 10;   break;
    }
    return 1;
}

static void atl_setup_mac(atl1c_t *a, uint32_t speed, int duplex) {
    uint32_t m = MAC_TX_EN | MAC_RX_EN | MAC_ADD_CRC | MAC_PAD | MAC_BC_EN |
                 (7u << MAC_PRMLEN_SHIFT);
    m |= ((speed == 1000) ? MAC_SPEED_1000 : MAC_SPEED_10_100) << MAC_SPEED_SHIFT;
    if (duplex) m |= MAC_DUPLX;
    aw32(a, ATL_MAC_CTRL, m);
}

static int atl_setup_rings(atl1c_t *a) {
    a->rfd = dma_alloc_coherent_low(ATL_NUM_RFD * sizeof(atl_rfd_t), &a->rfd_phys);
    a->rrd = dma_alloc_coherent_low(ATL_NUM_RFD * sizeof(atl_rrd_t), &a->rrd_phys);
    a->tpd = dma_alloc_coherent_low(ATL_NUM_TPD * sizeof(atl_tpd_t), &a->tpd_phys);
    if (!a->rfd || !a->rrd || !a->tpd) return -1;

    memset(a->rfd, 0, ATL_NUM_RFD * sizeof(atl_rfd_t));
    memset(a->rrd, 0, ATL_NUM_RFD * sizeof(atl_rrd_t));
    memset(a->tpd, 0, ATL_NUM_TPD * sizeof(atl_tpd_t));

    for (int i = 0; i < ATL_NUM_RFD; i++) {
        a->rx_buf[i] = dma_alloc_coherent_low(ATL_BUFSZ, &a->rx_buf_phys[i]);
        if (!a->rx_buf[i]) return -1;
        a->rfd[i].addr = a->rx_buf_phys[i];
    }
    for (int i = 0; i < ATL_NUM_TPD; i++) {
        a->tx_buf[i] = dma_alloc_coherent_low(ATL_BUFSZ, &a->tx_buf_phys[i]);
        if (!a->tx_buf[i]) return -1;
    }

    aw32(a, ATL_RX_BASE_ADDR_HI, (uint32_t)(a->rfd_phys >> 32));
    aw32(a, ATL_TX_BASE_ADDR_HI, (uint32_t)(a->tpd_phys >> 32));
    aw32(a, ATL_RFD0_HEAD_ADDR_LO, (uint32_t)(a->rfd_phys & 0xFFFFFFFFu));
    aw32(a, ATL_RRD0_HEAD_ADDR_LO, (uint32_t)(a->rrd_phys & 0xFFFFFFFFu));
    aw32(a, ATL_TPD_PRI0_ADDR_LO,  (uint32_t)(a->tpd_phys & 0xFFFFFFFFu));
    aw32(a, ATL_RFD_RING_SIZE, ATL_NUM_RFD & 0x0FFFu);
    aw32(a, ATL_RRD_RING_SIZE, ATL_NUM_RFD & 0x0FFFu);
    aw32(a, ATL_TPD_RING_SIZE, ATL_NUM_TPD & 0xFFFFu);
    aw32(a, ATL_RX_BUF_SIZE,   ATL_BUFSZ   & 0xFFFFu);
    aw32(a, ATL_MTU, 1500 + 18);
    aw32(a, ATL_RFD_FREE_THRESH, (ATL_NUM_RFD / 8) & 0x3Fu);

    a->rfd_prod = ATL_NUM_RFD - 1;
    a->rrd_next = 0;
    a->tpd_prod = 0;
    aw32(a, ATL_MB_RFD0_PROD_IDX, a->rfd_prod);
    aw16(a, ATL_TPD_PRI0_PIDX, 0);
    return 0;
}

static void atl_start_queues(atl1c_t *a) {
    uint32_t dma = (DMA_RORDER_MODE_OUT & 7u)
                 | DMA_RREQ_PRI_DATA
                 | (DMA_REQ_BLEN_128 << DMA_RREQ_BLEN_SHIFT)
                 | (DMA_REQ_BLEN_128 << DMA_WREQ_BLEN_SHIFT)
                 | (DMA_RDLY_CNT_DEF << DMA_RDLY_CNT_SHIFT)
                 | (DMA_WDLY_CNT_DEF << DMA_WDLY_CNT_SHIFT);
    aw32(a, ATL_DMA_CTRL, dma);
    aw32(a, ATL_TXQ_CTRL,
         TXQ_CTRL_EN | TXQ_CTRL_ENH_MODE |
         (TXQ_TXF_BURST_PREF << TXQ_TXF_BURST_SHIFT) |
         (TXQ_TPD_BURST_DEF << TXQ_TPD_BURST_SHIFT));
    aw32(a, ATL_RXQ_CTRL,
         RXQ_CTRL_EN | (RXQ_RFD_BURST_DEF << RXQ_RFD_BURST_SHIFT));
}

static int atl1c_transmit(netdev_t *nd, const void *frame, size_t len) {
    atl1c_t *a = nd->priv;
    if (len == 0 || len > ATL_BUFSZ) return -1;

    uint64_t f = spinlock_acquire_irqsave(&a->lock);

    uint16_t cons = ar16(a, ATL_TPD_PRI0_CIDX);
    uint16_t next = (uint16_t)((a->tpd_prod + 1) % ATL_NUM_TPD);
    if (next == cons) {
        spinlock_release_irqrestore(&a->lock, f);
        nd->tx_dropped++;
        return -1;
    }

    uint16_t i = a->tpd_prod;
    memcpy(a->tx_buf[i], frame, len);
    a->tpd[i].addr       = a->tx_buf_phys[i];
    a->tpd[i].buffer_len = (uint16_t)len;
    a->tpd[i].vlan_tag   = 0;
    a->tpd[i].word1      = TPD_EOP;

    a->tpd_prod = next;
    aw16(a, ATL_TPD_PRI0_PIDX, a->tpd_prod);

    nd->tx_packets++;
    nd->tx_bytes += len;
    spinlock_release_irqrestore(&a->lock, f);
    return 0;
}

static void atl1c_rx_drain(atl1c_t *a) {
    uint64_t rf = spinlock_acquire_irqsave(&a->lock);
    for (int guard = 0; guard < ATL_NUM_RFD * 2; guard++) {
        atl_rrd_t *r = &a->rrd[a->rrd_next];
        uint32_t w3 = r->word3;
        if (!(w3 & RRS_RXD_UPDATED)) break;

        uint32_t w0    = r->word0;
        uint32_t nbuf  = (w0 >> RRS_RFD_CNT_SHIFT) & RRS_RFD_CNT_MASK;
        uint32_t index = (w0 >> RRS_RFD_INDEX_SHIFT) & RRS_RFD_INDEX_MASK;
        uint32_t len   = w3 & RRS_PKT_SIZE_MASK;

        if (nbuf == 0) nbuf = 1;

        if (!(w3 & RRS_RX_ERR_SUM) && index < ATL_NUM_RFD &&
            len > ATL_ETH_FCS_LEN && len <= ATL_BUFSZ) {
            size_t plen = len - ATL_ETH_FCS_LEN;
            a->ndev->rx_packets++;
            a->ndev->rx_bytes += plen;
            net_rx(a->ndev, a->rx_buf[index], plen);
        } else {
            a->ndev->rx_dropped++;
        }

        r->word3 = 0;
        a->rrd_next = (uint16_t)((a->rrd_next + 1) % ATL_NUM_RFD);
        a->rfd_prod = (uint16_t)((index + nbuf) % ATL_NUM_RFD);
        aw32(a, ATL_MB_RFD0_PROD_IDX, a->rfd_prod);
    }
    spinlock_release_irqrestore(&a->lock, rf);
}

static void atl1c_link_poll(atl1c_t *a) {
    uint32_t speed = 0;
    int duplex = 0;
    int up = atl_link_state(a, &speed, &duplex);
    if (up != a->ndev->link_up) {
        a->ndev->link_up = up;
        if (up) {
            atl_setup_mac(a, speed, duplex);
            serial_printf("[atl1c] link up: %u Mbps %s-duplex\n",
                          speed, duplex ? "full" : "half");
        } else {
            serial_printf("[atl1c] link down\n");
        }
    }
}

static void atl1c_irq(void *ctx) {
    atl1c_t *a = ctx;
    uint32_t isr = ar32(a, ATL_ISR);
    if (!isr) return;
    aw32(a, ATL_ISR, ATL_ISR_DIS_INT);
    atl1c_rx_drain(a);
    aw32(a, ATL_ISR, 0);
}

static void atl1c_worker(void *arg) {
    (void)arg;
    int tick = 0;
    for (;;) {
        for (atl1c_t *a = g_nics; a; a = a->next) {
            atl1c_rx_drain(a);
            if ((tick % 500) == 0) atl1c_link_poll(a);
        }
        tick++;
        task_sleep_ms(1);
    }
}

static int atl1c_probe(pci_device_t *dev) {
    if (dev->bars[0].type != PCI_BAR_TYPE_MEM || !dev->bars[0].base) {
        serial_printf("[atl1c] BAR0 not MMIO, skipping\n");
        return -1;
    }

    uint16_t cmd = pci_config_read16(dev->segment, dev->bus, dev->device, dev->function, PCI_COMMAND);
    cmd |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
    pci_config_write16(dev->segment, dev->bus, dev->device, dev->function, PCI_COMMAND, cmd);

    atl1c_t *a = calloc(1, sizeof(*a));
    if (!a) return -1;
    a->regs = (volatile uint8_t *)mmio_map(dev->bars[0].base, dev->bars[0].size);
    if (!a->regs) { free(a); return -1; }

    uint32_t probe_reg = ar32(a, ATL_MASTER_CTRL);
    if (probe_reg == 0xFFFFFFFFu) {
        serial_printf("[atl1c] MMIO reads back all-ones at phys 0x%llx - BAR not usable\n",
                      (unsigned long long)dev->bars[0].base);
        free(a);
        return -1;
    }

    uint8_t mac[6];
    atl_read_mac(a, mac);

    if (atl_reset(a) != 0) { free(a); return -1; }
    aw32(a, ATL_CLK_GATING_CTRL, CLK_GATING_EN_ALL);
    atl_phy_init(a);
    atl_write_mac(a, mac);

    aw32(a, ATL_RX_HASH_TABLE, 0);
    aw32(a, ATL_RX_HASH_TABLE + 4, 0);

    if (atl_setup_rings(a) != 0) {
        serial_printf("[atl1c] ring allocation failed\n");
        free(a);
        return -1;
    }
    atl_start_queues(a);
    atl_setup_mac(a, 100, 1);

    a->ndev = netdev_register(mac, 1500, atl1c_transmit, a);
    if (!a->ndev) { free(a); return -1; }

    a->next = g_nics;
    g_nics = a;

    uint32_t speed = 0;
    int duplex = 0;
    a->ndev->link_up = atl_link_state(a, &speed, &duplex);
    if (a->ndev->link_up) atl_setup_mac(a, speed, duplex);

    int vec = irq_alloc_vector();
    if (vec > 0 && dev->cap_msi_off &&
        pci_enable_msi(dev, (uint8_t)vec, lapic_get_id()) == 0 &&
        irq_request(vec, atl1c_irq, a, "atl1c") == 0) {
        a->vector = vec;
        aw32(a, ATL_ISR, 0xFFFFFFFFu);
        aw32(a, ATL_IMR, 0x00010000u | 0x00008000u | 0x00000008u);
    } else {
        if (vec > 0) irq_free_vector(vec);
        a->polled = 1;
    }

    serial_printf("[atl1c] %s: dma_ctrl=0x%08x idle=0x%08x txq=0x%08x rxq=0x%08x clk=0x%08x\n",
                  a->ndev->name, ar32(a, ATL_DMA_CTRL), ar32(a, ATL_IDLE_STATUS),
                  ar32(a, ATL_TXQ_CTRL), ar32(a, ATL_RXQ_CTRL),
                  ar32(a, ATL_CLK_GATING_CTRL));
    serial_printf("[atl1c] %s: MAC %02x:%02x:%02x:%02x:%02x:%02x link=%s irq=%s\n",
                  a->ndev->name, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  a->ndev->link_up ? "up" : "down",
                  a->polled ? "poll" : "msi");
    return 0;
}

static const pci_driver_t g_atl1c_driver = {
    .name           = "atl1c",
    .match_vendor   = 0x1969,
    .match_device   = -1,
    .match_class    = 0x02,
    .match_subclass = 0x00,
    .probe          = atl1c_probe,
};

void atl1c_init(void) {
    pci_register_driver(&g_atl1c_driver);
}

void atl1c_start_worker(void) {
    if (g_nics) task_create("atl1c_worker", atl1c_worker, NULL, 1);
}
