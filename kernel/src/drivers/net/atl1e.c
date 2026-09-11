#include "../../../include/drivers/net/atl1e.h"
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

#define ATE_PCIE_PHYMISC        0x1000
#define PCIE_PHYMISC_FORCE_RCV_DET  0x4
#define ATE_PCIE_DLL_TX_CTRL1   0x1008
#define PCIE_DLL_TX_CTRL1_SEL   0x8000
#define ATE_LTSSM_TEST_MODE     0x12FC
#define LTSSM_TEST_MODE_DEF     0xE000

#define ATE_MASTER_CTRL         0x1400
#define MASTER_SOFT_RST         0x1
#define MASTER_MTIMER_EN        0x2
#define MASTER_ITIMER_EN        0x4
#define MASTER_ITIMER2_EN       0x20
#define MASTER_INT_RDCLR        0x40
#define MASTER_LED_MODE         0x200

#define ATE_IRQ_MODU_TIMER_INIT  0x1408
#define ATE_IRQ_MODU_TIMER2_INIT 0x140A
#define ATE_GPHY_CTRL           0x140C
#define GPHY_EXT_RESET          0x0001
#define GPHY_HIB_EN             0x0400
#define GPHY_HIB_PULSE          0x0800
#define GPHY_SEL_ANA_RST        0x1000
#define GPHY_PHY_PLL_ON         0x2000
#define GPHY_CTRL_DEFAULT       (GPHY_PHY_PLL_ON | GPHY_SEL_ANA_RST | \
                                 GPHY_HIB_PULSE  | GPHY_HIB_EN)

#define ATE_CMBDISDMA_TIMER     0x140E
#define ATE_IDLE_STATUS         0x1410
#define ATE_MDIO_CTRL           0x1414

#define MDIO_DATA_MASK          0xFFFFu
#define MDIO_REG_ADDR_SHIFT     16
#define MDIO_RW                 0x00200000u
#define MDIO_SUP_PREAMBLE       0x00400000u
#define MDIO_START              0x00800000u
#define MDIO_CLK_SEL_SHIFT      24
#define MDIO_CLK_25_4           0u
#define MDIO_BUSY               0x08000000u

#define ATE_MAC_CTRL            0x1480
#define MAC_TX_EN               0x1
#define MAC_RX_EN               0x2
#define MAC_TX_FLOW             0x4
#define MAC_RX_FLOW             0x8
#define MAC_DUPLX               0x20
#define MAC_ADD_CRC             0x40
#define MAC_PAD                 0x80
#define MAC_PRMLEN_SHIFT        10
#define MAC_PROMIS_EN           0x8000
#define MAC_SPEED_SHIFT         20
#define MAC_SPEED_1000          2u
#define MAC_SPEED_10_100        1u
#define MAC_MC_ALL_EN           0x2000000
#define MAC_BC_EN               0x4000000

#define ATE_MAC_STA_ADDR        0x1488
#define ATE_RX_HASH_TABLE       0x1490
#define ATE_MTU                 0x149C
#define ATE_WOL_CTRL            0x14A0

#define ATE_SRAM_RXF_LEN        0x1524
#define ATE_LOAD_PTR            0x1534

#define ATE_DESC_BASE_ADDR_HI   0x1540
#define ATE_RXF0_BASE_ADDR_HI   0x1540
#define ATE_HOST_RXF0_PAGE0_LO  0x1544
#define ATE_HOST_RXF0_PAGE1_LO  0x1548
#define ATE_TPD_BASE_ADDR_LO    0x154C
#define ATE_HOST_RXFPAGE_SIZE   0x1558
#define ATE_TPD_RING_SIZE       0x155C

#define ATE_TXQ_CTRL            0x1580
#define TXQ_NUM_TPD_BURST_SHIFT 0
#define TXQ_CTRL_EN             0x20
#define TXQ_CTRL_ENH_MODE       0x40

#define ATE_TX_EARLY_TH         0x1584

#define ATE_RXQ_CTRL            0x15A0
#define RXQ_IPV6_XSUM_VERIFY_EN 0x80
#define RXQ_CTRL_CUT_THRU_EN    0x40000000u
#define RXQ_CTRL_EN             0x80000000u

#define ATE_RXQ_JMBOSZ_RRDTIM   0x15A4
#define ATE_RXQ_RXF_PAUSE_THRESH 0x15A8

#define ATE_DMA_CTRL            0x15C0
#define DMA_DMAR_OUT_ORDER      0x4
#define DMA_DMAR_BURST_LEN_SHIFT 4
#define DMA_DMAW_BURST_LEN_SHIFT 7
#define DMA_DMAR_REQ_PRI        0x400
#define DMA_DMAR_DLY_CNT_SHIFT  11
#define DMA_DMAW_DLY_CNT_SHIFT  16
#define DMA_CTRL_RXCMB_EN       0x200000

#define ATE_SMB_STAT_TIMER      0x15C4
#define ATE_TRIG_TPD_THRESH     0x15C8
#define ATE_TRIG_RRD_THRESH     0x15CA
#define ATE_TRIG_TXTIMER        0x15CC
#define ATE_TRIG_RXTIMER        0x15CE

#define ATE_MB_TPD_PROD_IDX     0x15F0
#define ATE_HOST_RXF0_PAGE0_VLD 0x15F4
#define ATE_HOST_RXF0_PAGE1_VLD 0x15F5

#define ATE_ISR                 0x1600
#define ATE_IMR                 0x1604
#define ISR_RX_PKT              0x00010000u
#define ISR_TX_PKT              0x00020000u
#define ISR_HW_RXF_OV           0x00000008u
#define ISR_HOST_RXF0_OV        0x00000010u
#define ISR_GPHY                0x00001000u
#define ISR_PHY_LINKDOWN        0x10000000u
#define ISR_DIS_INT             0x80000000u

#define ATE_TPD_CONS_IDX        0x1804
#define ATE_HOST_RXF0_MB0_LO    0x1820
#define ATE_HOST_RXF0_MB1_LO    0x1824
#define ATE_HOST_TX_CMB_LO      0x1840

#define MII_BMCR                0x00
#define MII_BMSR                0x01
#define MII_ADVERTISE           0x04
#define MII_CTRL1000            0x09
#define MII_INT_CTRL            0x12
#define MII_PSSR                0x11
#define MII_DBG_ADDR            0x1D
#define MII_DBG_DATA            0x1E

#define BMCR_RESET              0x8000
#define BMCR_ANENABLE           0x1000
#define BMCR_ANRESTART          0x0200
#define BMSR_LSTATUS            0x0004

#define ADVERTISE_DEFAULT       0x0DE1
#define CTRL1000_DEFAULT        0x0300

#define PSSR_RESOLVED           0x0800
#define PSSR_DPLX               0x2000
#define PSSR_SPEED_MASK         0xC000
#define PSSR_100MBS             0x4000
#define PSSR_1000MBS            0x8000

#define RRS_PKT_SIZE_SHIFT      16
#define RRS_PKT_SIZE_MASK       0x3FFFu
#define RRS_IS_ERR_FRAME        0x0200

#define TPD_BUFLEN_MASK         0x3FFFu
#define TPD_EOP                 0x1u

#define ATE_NUM_TPD             64
#define ATE_TX_BUFSZ            2048
#define ATE_PAGE_SIZE           16384u
#define ATE_MAX_FRAME           1500
#define ATE_RRS_SIZE            16

typedef struct __attribute__((packed)) {
    uint64_t buffer_addr;
    uint32_t word2;
    uint32_t word3;
} ate_tpd_t;

typedef struct {
    uint8_t  *addr;
    uintptr_t phys;
    volatile uint32_t *wptr;
    uintptr_t wptr_phys;
    uint32_t  read_offset;
} ate_page_t;

typedef struct atl1e_dev {
    volatile uint8_t *regs;
    netdev_t *ndev;

    ate_tpd_t *tpd;   uintptr_t tpd_phys;
    uint8_t   *tx_buf[ATE_NUM_TPD];
    uintptr_t  tx_buf_phys[ATE_NUM_TPD];
    uint16_t   tpd_prod;

    ate_page_t page[2];
    uint8_t    rx_using;
    uint16_t   rx_nxseq;
    uint32_t   page_size;
    uint32_t   real_page_size;

    uint8_t   *tx_cmb;  uintptr_t tx_cmb_phys;

    int        vector;
    int        polled;
    int        draining;
    int        seq_errs;
    uint64_t   last_report_ms;
    uint64_t   last_rx, last_tx, last_drop;
    uint32_t   isr_seen;
    spinlock_t lock;
    struct atl1e_dev *next;
} atl1e_t;

static atl1e_t *g_nics;

static inline uint32_t ar32(atl1e_t *a, uint32_t r) {
    return *(volatile uint32_t *)(a->regs + r);
}
static inline void aw32(atl1e_t *a, uint32_t r, uint32_t v) {
    *(volatile uint32_t *)(a->regs + r) = v;
}
static inline uint16_t ar16(atl1e_t *a, uint32_t r) {
    return *(volatile uint16_t *)(a->regs + r);
}
static inline void aw16(atl1e_t *a, uint32_t r, uint16_t v) {
    *(volatile uint16_t *)(a->regs + r) = v;
}
static inline void aw8(atl1e_t *a, uint32_t r, uint8_t v) {
    *(volatile uint8_t *)(a->regs + r) = v;
}

static void ate_delay(uint32_t loops) {
    for (volatile uint32_t i = 0; i < loops * 1000u; i++) { }
}

static int ate_mdio_read(atl1e_t *a, uint8_t reg, uint16_t *out) {
    uint32_t v = MDIO_RW | MDIO_SUP_PREAMBLE | MDIO_START |
                 (MDIO_CLK_25_4 << MDIO_CLK_SEL_SHIFT) |
                 (((uint32_t)reg & 0x1F) << MDIO_REG_ADDR_SHIFT);
    aw32(a, ATE_MDIO_CTRL, v);
    for (int i = 0; i < 30; i++) {
        ate_delay(2);
        v = ar32(a, ATE_MDIO_CTRL);
        if (!(v & (MDIO_START | MDIO_BUSY))) {
            *out = (uint16_t)(v & MDIO_DATA_MASK);
            return 0;
        }
    }
    return -1;
}

static int ate_mdio_write(atl1e_t *a, uint8_t reg, uint16_t val) {
    uint32_t v = MDIO_SUP_PREAMBLE | MDIO_START |
                 (MDIO_CLK_25_4 << MDIO_CLK_SEL_SHIFT) |
                 (((uint32_t)reg & 0x1F) << MDIO_REG_ADDR_SHIFT) |
                 ((uint32_t)val & MDIO_DATA_MASK);
    aw32(a, ATE_MDIO_CTRL, v);
    for (int i = 0; i < 30; i++) {
        ate_delay(2);
        if (!(ar32(a, ATE_MDIO_CTRL) & (MDIO_START | MDIO_BUSY))) return 0;
    }
    return -1;
}

static int ate_reset(atl1e_t *a) {
    aw32(a, ATE_MASTER_CTRL, MASTER_LED_MODE | MASTER_SOFT_RST);
    ate_delay(2);
    for (int i = 0; i < 100; i++) {
        if (ar32(a, ATE_IDLE_STATUS) == 0) return 0;
        ate_delay(2);
    }
    serial_printf("[atl1e] reset timeout, idle=0x%x\n", ar32(a, ATE_IDLE_STATUS));
    return -1;
}

static void ate_init_pcie(atl1e_t *a) {
    aw32(a, ATE_LTSSM_TEST_MODE, LTSSM_TEST_MODE_DEF);
    uint32_t v = ar32(a, ATE_PCIE_DLL_TX_CTRL1);
    aw32(a, ATE_PCIE_DLL_TX_CTRL1, v | PCIE_DLL_TX_CTRL1_SEL);
}

static void ate_phy_init(atl1e_t *a) {
    aw16(a, ATE_GPHY_CTRL, GPHY_CTRL_DEFAULT);
    ate_delay(4);
    aw16(a, ATE_GPHY_CTRL, GPHY_CTRL_DEFAULT | GPHY_EXT_RESET);
    ate_delay(4);

    ate_mdio_write(a, MII_DBG_ADDR, 0x000B);
    ate_mdio_write(a, MII_DBG_DATA, 0xBC00);
    ate_mdio_write(a, MII_DBG_ADDR, 0x0000);
    ate_mdio_write(a, MII_DBG_DATA, 0x02EF);
    ate_mdio_write(a, MII_DBG_ADDR, 0x0012);
    ate_mdio_write(a, MII_DBG_DATA, 0x4C04);
    ate_mdio_write(a, MII_DBG_ADDR, 0x0004);
    ate_mdio_write(a, MII_DBG_DATA, 0x8BBB);
    ate_mdio_write(a, MII_DBG_ADDR, 0x0005);
    ate_mdio_write(a, MII_DBG_DATA, 0x2C46);
    ate_delay(2);

    ate_mdio_write(a, MII_INT_CTRL, 0x0C00);

    ate_mdio_write(a, MII_ADVERTISE, ADVERTISE_DEFAULT);
    ate_mdio_write(a, MII_CTRL1000, CTRL1000_DEFAULT);
    ate_mdio_write(a, MII_BMCR, BMCR_RESET | BMCR_ANENABLE | BMCR_ANRESTART);
    ate_delay(20);
}

static void ate_read_mac(atl1e_t *a, uint8_t mac[6]) {
    uint32_t hi = ar32(a, ATE_MAC_STA_ADDR + 4);
    uint32_t lo = ar32(a, ATE_MAC_STA_ADDR);
    mac[0] = (uint8_t)(hi >> 8);
    mac[1] = (uint8_t)(hi);
    mac[2] = (uint8_t)(lo >> 24);
    mac[3] = (uint8_t)(lo >> 16);
    mac[4] = (uint8_t)(lo >> 8);
    mac[5] = (uint8_t)(lo);
}

static void ate_write_mac(atl1e_t *a, const uint8_t mac[6]) {
    aw32(a, ATE_MAC_STA_ADDR,
         ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) |
         ((uint32_t)mac[4] << 8)  | (uint32_t)mac[5]);
    aw32(a, ATE_MAC_STA_ADDR + 4,
         ((uint32_t)mac[0] << 8) | (uint32_t)mac[1]);
}

static int ate_link_state(atl1e_t *a, uint32_t *speed, int *duplex) {
    uint16_t bmsr = 0, pssr = 0;
    if (ate_mdio_read(a, MII_BMSR, &bmsr) != 0) return 0;
    if (ate_mdio_read(a, MII_BMSR, &bmsr) != 0) return 0;
    if (!(bmsr & BMSR_LSTATUS)) return 0;
    if (ate_mdio_read(a, MII_PSSR, &pssr) != 0) return 0;
    if (!(pssr & PSSR_RESOLVED)) return 0;
    *duplex = (pssr & PSSR_DPLX) ? 1 : 0;
    switch (pssr & PSSR_SPEED_MASK) {
        case PSSR_1000MBS: *speed = 1000; break;
        case PSSR_100MBS:  *speed = 100;  break;
        default:           *speed = 10;   break;
    }
    return 1;
}

static void ate_setup_mac(atl1e_t *a, uint32_t speed, int duplex) {
    uint32_t v = MAC_TX_EN | MAC_RX_EN;
    if (duplex) v |= MAC_DUPLX;
    v |= ((speed == 1000) ? MAC_SPEED_1000 : MAC_SPEED_10_100) << MAC_SPEED_SHIFT;
    v |= MAC_TX_FLOW | MAC_RX_FLOW;
    v |= MAC_ADD_CRC | MAC_PAD;
    v |= (7u << MAC_PRMLEN_SHIFT);
    v |= MAC_BC_EN;
    aw32(a, ATE_MAC_CTRL, v);
}

static int ate_alloc_rings(atl1e_t *a) {
    a->page_size = ATE_PAGE_SIZE;
    a->real_page_size = (a->page_size + ATE_MAX_FRAME + 14 + 4 + 4 + 31) & ~31u;

    a->tpd = dma_alloc_coherent_low(ATE_NUM_TPD * sizeof(ate_tpd_t), &a->tpd_phys);
    if (!a->tpd) return -1;
    memset(a->tpd, 0, ATE_NUM_TPD * sizeof(ate_tpd_t));

    for (int i = 0; i < ATE_NUM_TPD; i++) {
        a->tx_buf[i] = dma_alloc_coherent_low(ATE_TX_BUFSZ, &a->tx_buf_phys[i]);
        if (!a->tx_buf[i]) return -1;
    }

    a->tx_cmb = dma_alloc_coherent_low(64, &a->tx_cmb_phys);
    if (!a->tx_cmb) return -1;
    memset(a->tx_cmb, 0, 64);

    for (int i = 0; i < 2; i++) {
        uintptr_t p = 0;
        a->page[i].addr = dma_alloc_coherent_low(a->real_page_size, &p);
        if (!a->page[i].addr) return -1;
        a->page[i].phys = p;
        memset(a->page[i].addr, 0, a->real_page_size);

        uintptr_t wp = 0;
        a->page[i].wptr = dma_alloc_coherent_low(64, &wp);
        if (!a->page[i].wptr) return -1;
        a->page[i].wptr_phys = wp;
        *a->page[i].wptr = 0;
        a->page[i].read_offset = 0;
    }

    uintptr_t spans[6] = {
        a->tpd_phys, a->tx_cmb_phys,
        a->page[0].phys, a->page[0].wptr_phys,
        a->page[1].phys, a->page[1].wptr_phys,
    };
    for (int i = 0; i < 6; i++) {
        if ((spans[i] >> 32) != 0) {
            serial_printf("[atl1e] DMA buffer above 4G (0x%llx); the chip shares one "
                          "high-address register for every ring\n",
                          (unsigned long long)spans[i]);
            return -1;
        }
    }
    for (int i = 0; i < ATE_NUM_TPD; i++) {
        if ((a->tx_buf_phys[i] >> 32) != 0) {
            serial_printf("[atl1e] TX buffer above 4G (0x%llx)\n",
                          (unsigned long long)a->tx_buf_phys[i]);
            return -1;
        }
    }
    return 0;
}

static void ate_configure(atl1e_t *a, const uint8_t mac[6]) {
    aw32(a, ATE_ISR, 0xFFFFFFFFu);
    ate_write_mac(a, mac);
    aw32(a, ATE_RX_HASH_TABLE, 0);
    aw32(a, ATE_RX_HASH_TABLE + 4, 0);
    aw32(a, ATE_WOL_CTRL, 0);

    aw32(a, ATE_DESC_BASE_ADDR_HI, (uint32_t)(a->tpd_phys >> 32));
    aw32(a, ATE_TPD_BASE_ADDR_LO, (uint32_t)(a->tpd_phys & 0xFFFFFFFFu));
    aw32(a, ATE_TPD_RING_SIZE, ATE_NUM_TPD);
    aw32(a, ATE_HOST_TX_CMB_LO, (uint32_t)(a->tx_cmb_phys & 0xFFFFFFFFu));

    aw32(a, ATE_RXF0_BASE_ADDR_HI, (uint32_t)(a->page[0].phys >> 32));
    aw32(a, ATE_HOST_RXF0_PAGE0_LO, (uint32_t)(a->page[0].phys & 0xFFFFFFFFu));
    aw32(a, ATE_HOST_RXF0_PAGE1_LO, (uint32_t)(a->page[1].phys & 0xFFFFFFFFu));
    aw32(a, ATE_HOST_RXF0_MB0_LO, (uint32_t)(a->page[0].wptr_phys & 0xFFFFFFFFu));
    aw32(a, ATE_HOST_RXF0_MB1_LO, (uint32_t)(a->page[1].wptr_phys & 0xFFFFFFFFu));
    aw8(a, ATE_HOST_RXF0_PAGE0_VLD, 1);
    aw8(a, ATE_HOST_RXF0_PAGE1_VLD, 1);

    aw32(a, ATE_HOST_RXFPAGE_SIZE, a->page_size);
    aw32(a, ATE_LOAD_PTR, 1);

    aw16(a, ATE_IRQ_MODU_TIMER_INIT, 100);
    aw16(a, ATE_IRQ_MODU_TIMER2_INIT, 100);
    aw32(a, ATE_MASTER_CTRL,
         MASTER_LED_MODE | MASTER_ITIMER_EN | MASTER_ITIMER2_EN);

    aw16(a, ATE_TRIG_RRD_THRESH, 1);
    aw16(a, ATE_TRIG_TPD_THRESH, ATE_NUM_TPD / 2);
    aw16(a, ATE_TRIG_RXTIMER, 4);
    aw16(a, ATE_TRIG_TXTIMER, 133);
    aw16(a, ATE_CMBDISDMA_TIMER, 50000);

    aw32(a, ATE_MTU, ATE_MAX_FRAME + 14 + 4 + 4);

    uint32_t jumbo_thresh = ATE_MAX_FRAME + 14 + 4 + 4;
    aw32(a, ATE_TX_EARLY_TH, (jumbo_thresh + 7) >> 3);
    aw16(a, ATE_TXQ_CTRL, (uint16_t)((5u << TXQ_NUM_TPD_BURST_SHIFT) |
                                     TXQ_CTRL_ENH_MODE | TXQ_CTRL_EN));

    aw16(a, ATE_RXQ_JMBOSZ_RRDTIM,
         (uint16_t)((((ATE_MAX_FRAME + 22 + 7) >> 3) & 0x7FF) | (1u << 11)));
    uint32_t rxf_len = ar32(a, ATE_SRAM_RXF_LEN);
    uint32_t rxf_high = rxf_len * 4 / 5;
    uint32_t rxf_low  = rxf_len / 5;
    aw32(a, ATE_RXQ_RXF_PAUSE_THRESH,
         ((rxf_high & 0xFFF) << 0) | ((rxf_low & 0xFFF) << 16));
    aw32(a, ATE_RXQ_CTRL,
         RXQ_IPV6_XSUM_VERIFY_EN | RXQ_CTRL_CUT_THRU_EN | RXQ_CTRL_EN);

    uint32_t dma = DMA_CTRL_RXCMB_EN
                 | ((3u & 7u) << DMA_DMAR_BURST_LEN_SHIFT)
                 | ((3u & 7u) << DMA_DMAW_BURST_LEN_SHIFT)
                 | DMA_DMAR_REQ_PRI | DMA_DMAR_OUT_ORDER
                 | ((15u & 0x1F) << DMA_DMAR_DLY_CNT_SHIFT)
                 | ((4u  & 0x0F) << DMA_DMAW_DLY_CNT_SHIFT);
    aw32(a, ATE_DMA_CTRL, dma);

    aw32(a, ATE_SMB_STAT_TIMER, 200000);
    aw32(a, ATE_ISR, 0x7FFFFFFFu);
}

static int atl1e_transmit(netdev_t *nd, const void *frame, size_t len) {
    atl1e_t *a = nd->priv;
    if (len == 0 || len > ATE_TX_BUFSZ) return -1;
    if (!nd->link_up) { nd->tx_dropped++; return -1; }

    uint64_t f = spinlock_acquire_irqsave(&a->lock);

    uint16_t cons = ar16(a, ATE_TPD_CONS_IDX);
    uint16_t next = (uint16_t)((a->tpd_prod + 1) % ATE_NUM_TPD);
    if (next == cons) {
        spinlock_release_irqrestore(&a->lock, f);
        nd->tx_dropped++;
        return -1;
    }

    uint16_t idx = a->tpd_prod;
    memcpy(a->tx_buf[idx], frame, len);

    ate_tpd_t *t = &a->tpd[idx];
    t->buffer_addr = a->tx_buf_phys[idx];
    t->word2 = (uint32_t)(len & TPD_BUFLEN_MASK);
    t->word3 = TPD_EOP;

    a->tpd_prod = next;
    __asm__ volatile ("sfence" ::: "memory");
    aw32(a, ATE_MB_TPD_PROD_IDX, a->tpd_prod);

    nd->tx_packets++;
    nd->tx_bytes += len;
    spinlock_release_irqrestore(&a->lock, f);
    return 0;
}

static void ate_page_recycle(atl1e_t *a, ate_page_t *pg) {
    pg->read_offset = 0;
    *pg->wptr = 0;
    aw8(a, a->rx_using ? ATE_HOST_RXF0_PAGE1_VLD : ATE_HOST_RXF0_PAGE0_VLD, 1);
    a->rx_using ^= 1;
}

static void atl1e_rx_drain(atl1e_t *a) {
    if (!a->ndev) return;
    uint64_t f = spinlock_acquire_irqsave(&a->lock);
    if (a->draining) { spinlock_release_irqrestore(&a->lock, f); return; }
    a->draining = 1;

    for (int guard = 0; guard < 256; guard++) {
        ate_page_t *pg = &a->page[a->rx_using];
        uint32_t write_offset = *pg->wptr;
        if (pg->read_offset >= write_offset) break;

        if (pg->read_offset + ATE_RRS_SIZE > a->real_page_size) {
            ate_page_recycle(a, pg);
            continue;
        }

        const uint8_t *rrs = pg->addr + pg->read_offset;
        uint16_t seq = *(const uint16_t *)(rrs + 0);
        uint32_t word1 = *(const uint32_t *)(rrs + 4);
        uint16_t pkt_flag = *(const uint16_t *)(rrs + 8);

        if (seq != a->rx_nxseq) {
            if (a->seq_errs < 8)
                serial_printf("[atl1e] rx sequence error (got %u want %u), "
                              "dropping the rest of the page\n", seq, a->rx_nxseq);
            a->seq_errs++;
            a->rx_nxseq = (uint16_t)(seq + 1);
            a->ndev->rx_dropped++;
            ate_page_recycle(a, pg);
            continue;
        }
        a->rx_nxseq++;

        uint32_t raw_size = (word1 >> RRS_PKT_SIZE_SHIFT) & RRS_PKT_SIZE_MASK;

        if (!(pkt_flag & RRS_IS_ERR_FRAME) && raw_size > 4) {
            uint32_t plen = raw_size - 4;
            if (plen <= ATE_MAX_FRAME + 14 &&
                pg->read_offset + ATE_RRS_SIZE + plen <= a->real_page_size) {
                a->ndev->rx_packets++;
                a->ndev->rx_bytes += plen;
                spinlock_release_irqrestore(&a->lock, f);
                net_rx(a->ndev, rrs + ATE_RRS_SIZE, plen);
                f = spinlock_acquire_irqsave(&a->lock);
            } else {
                a->ndev->rx_dropped++;
            }
        } else if (pkt_flag & RRS_IS_ERR_FRAME) {
            a->ndev->rx_dropped++;
        }

        pg->read_offset += (raw_size + ATE_RRS_SIZE + 31) & ~31u;

        if (pg->read_offset >= a->page_size) ate_page_recycle(a, pg);
    }

    a->draining = 0;
    spinlock_release_irqrestore(&a->lock, f);
}

static void atl1e_link_poll(atl1e_t *a) {
    uint32_t speed = 0;
    int duplex = 0;
    int up = ate_link_state(a, &speed, &duplex);
    if (up == a->ndev->link_up) return;
    a->ndev->link_up = up;
    if (up) {
        ate_setup_mac(a, speed, duplex);
        serial_printf("[atl1e] %s: link up, %u Mbps %s-duplex\n",
                      a->ndev->name, speed, duplex ? "full" : "half");
    } else {
        serial_printf("[atl1e] %s: link down\n", a->ndev->name);
    }
}

static void atl1e_irq(void *ctx) {
    atl1e_t *a = ctx;
    uint32_t isr = ar32(a, ATE_ISR);
    if (!isr) return;
    aw32(a, ATE_ISR, isr | ISR_DIS_INT);
    if (isr & (ISR_RX_PKT | ISR_HOST_RXF0_OV | ISR_HW_RXF_OV))
        atl1e_rx_drain(a);
    aw32(a, ATE_ISR, 0);
}

static void atl1e_report(atl1e_t *a) {
    netdev_t *nd = a->ndev;
    if (!nd) return;
    if (nd->rx_packets == a->last_rx && nd->tx_packets == a->last_tx &&
        nd->rx_dropped == a->last_drop)
        return;
    a->last_rx = nd->rx_packets;
    a->last_tx = nd->tx_packets;
    a->last_drop = nd->rx_dropped;

    LOG_D("[atl1e] %s rx=%llu tx=%llu rxdrop=%llu txdrop=%llu seqerr=%d "
          "using=%u p0(w=%u r=%u) p1(w=%u r=%u) isr=0x%08x tpd=%u/%u idle=0x%x\n",
          nd->name,
          (unsigned long long)nd->rx_packets, (unsigned long long)nd->tx_packets,
          (unsigned long long)nd->rx_dropped, (unsigned long long)nd->tx_dropped,
          a->seq_errs, (unsigned)a->rx_using,
          (unsigned)*a->page[0].wptr, (unsigned)a->page[0].read_offset,
          (unsigned)*a->page[1].wptr, (unsigned)a->page[1].read_offset,
          a->isr_seen, (unsigned)a->tpd_prod,
          (unsigned)ar16(a, ATE_TPD_CONS_IDX), ar32(a, ATE_IDLE_STATUS));
    a->isr_seen = 0;
}

static void atl1e_worker(void *arg) {
    (void)arg;
    int tick = 0;
    for (;;) {
        for (atl1e_t *a = g_nics; a; a = a->next) {
            uint32_t isr = ar32(a, ATE_ISR);
            if (isr && isr != ISR_DIS_INT) {
                a->isr_seen |= isr & ~ISR_DIS_INT;
                aw32(a, ATE_ISR, isr);
            }
            atl1e_rx_drain(a);
            if ((tick % 500) == 0) atl1e_link_poll(a);
            if ((tick % 2500) == 0) atl1e_report(a);
        }
        tick++;
        task_sleep_ms(1);
    }
}

static int atl1e_probe(pci_device_t *dev) {
    serial_printf("[atl1e] probe %02x:%02x.%u vendor=%04x device=%04x\n",
                  dev->bus, dev->device, dev->function,
                  dev->vendor_id, dev->device_id);

    if (dev->bars[0].type != PCI_BAR_TYPE_MEM || !dev->bars[0].base) {
        serial_printf("[atl1e] BAR0 not MMIO, skipping\n");
        return -1;
    }

    pci_power_up(dev);
    pci_disable_aspm(dev);

    uint16_t cmd = pci_config_read16(dev->segment, dev->bus, dev->device,
                                     dev->function, PCI_COMMAND);
    cmd |= PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
    pci_config_write16(dev->segment, dev->bus, dev->device, dev->function,
                       PCI_COMMAND, cmd);

    atl1e_t *a = calloc(1, sizeof(*a));
    if (!a) return -1;
    a->regs = (volatile uint8_t *)mmio_map(dev->bars[0].base, dev->bars[0].size);
    if (!a->regs) { free(a); return -1; }

    if (ar32(a, ATE_MASTER_CTRL) == 0xFFFFFFFFu) {
        serial_printf("[atl1e] MMIO reads all-ones, BAR not usable\n");
        free(a);
        return -1;
    }

    uint32_t phym = ar32(a, ATE_PCIE_PHYMISC);
    aw32(a, ATE_PCIE_PHYMISC, phym | PCIE_PHYMISC_FORCE_RCV_DET);

    uint8_t mac[6];
    ate_read_mac(a, mac);

    if (ate_reset(a) != 0) { free(a); return -1; }
    ate_init_pcie(a);
    aw32(a, ATE_RX_HASH_TABLE, 0);
    aw32(a, ATE_RX_HASH_TABLE + 4, 0);
    ate_phy_init(a);

    if (ate_alloc_rings(a) != 0) {
        serial_printf("[atl1e] ring allocation failed\n");
        free(a);
        return -1;
    }
    ate_configure(a, mac);

    a->ndev = netdev_register(mac, ATE_MAX_FRAME, atl1e_transmit, a);
    if (!a->ndev) { free(a); return -1; }
    a->next = g_nics;
    g_nics = a;

    uint32_t speed = 0;
    int duplex = 0;
    a->ndev->link_up = ate_link_state(a, &speed, &duplex);
    ate_setup_mac(a, a->ndev->link_up ? speed : 100,
                  a->ndev->link_up ? duplex : 1);

    int vec = irq_alloc_vector();
    if (vec > 0 && dev->cap_msi_off &&
        pci_enable_msi(dev, (uint8_t)vec, lapic_get_id()) == 0 &&
        irq_request(vec, atl1e_irq, a, "atl1e") == 0) {
        a->vector = vec;
        aw32(a, ATE_ISR, 0xFFFFFFFFu);
        aw32(a, ATE_IMR, ISR_RX_PKT | ISR_TX_PKT | ISR_HOST_RXF0_OV |
                         ISR_HW_RXF_OV | ISR_GPHY | ISR_PHY_LINKDOWN);
    } else {
        if (vec > 0) irq_free_vector(vec);
        a->polled = 1;
    }

    serial_printf("[atl1e] %s: txq=0x%04x rxq=0x%08x dma=0x%08x idle=0x%08x "
                  "pagesz=%u\n",
                  a->ndev->name, ar16(a, ATE_TXQ_CTRL), ar32(a, ATE_RXQ_CTRL),
                  ar32(a, ATE_DMA_CTRL), ar32(a, ATE_IDLE_STATUS), a->page_size);
    serial_printf("[atl1e] %s: MAC %02x:%02x:%02x:%02x:%02x:%02x link=%s irq=%s\n",
                  a->ndev->name, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  a->ndev->link_up ? "up" : "down",
                  a->polled ? "poll" : "msi");
    return 0;
}

static const pci_driver_t g_atl1e_driver = {
    .name           = "atl1e",
    .match_vendor   = 0x1969,
    .match_device   = 0x1026,
    .match_class    = 0x02,
    .match_subclass = 0x00,
    .probe          = atl1e_probe,
};

void atl1e_init(void) {
    pci_register_driver(&g_atl1e_driver);
}

void atl1e_start_worker(void) {
    if (g_nics) task_create("atl1e_worker", atl1e_worker, NULL, 1);
}
