#include "../../../include/drivers/net/e1000.h"
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

#define E1000_CTRL    0x0000
#define E1000_STATUS  0x0008
#define E1000_EERD    0x0014
#define E1000_ICR     0x00C0
#define E1000_IMS     0x00D0
#define E1000_IMC     0x00D8
#define E1000_RCTL    0x0100
#define E1000_TCTL    0x0400
#define E1000_TIPG    0x0410
#define E1000_RDBAL   0x2800
#define E1000_RDBAH   0x2804
#define E1000_RDLEN   0x2808
#define E1000_RDH     0x2810
#define E1000_RDT     0x2818
#define E1000_TDBAL   0x3800
#define E1000_TDBAH   0x3804
#define E1000_TDLEN   0x3808
#define E1000_TDH     0x3810
#define E1000_TDT     0x3818
#define E1000_MTA     0x5200
#define E1000_RAL     0x5400
#define E1000_RAH     0x5404

#define CTRL_SLU      (1u << 6)
#define CTRL_ASDE     (1u << 5)
#define CTRL_RST      (1u << 26)

#define STATUS_LU     (1u << 1)

#define RCTL_EN       (1u << 1)
#define RCTL_BAM      (1u << 15)
#define RCTL_SECRC    (1u << 26)

#define TCTL_EN       (1u << 1)
#define TCTL_PSP      (1u << 3)
#define TCTL_CT_SHIFT   4
#define TCTL_COLD_SHIFT 12

#define TXD_CMD_EOP   (1u << 0)
#define TXD_CMD_IFCS  (1u << 1)
#define TXD_CMD_RS    (1u << 3)
#define TXD_STAT_DD   (1u << 0)

#define RXD_STAT_DD   (1u << 0)

#define IM_LSC        (1u << 2)
#define IM_RXDMT0     (1u << 4)
#define IM_RXO        (1u << 6)
#define IM_RXT0       (1u << 7)

#define E1000_NUM_RX  32
#define E1000_NUM_TX  8
#define E1000_BUFSZ   2048

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} e1000_rx_desc_t;

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} e1000_tx_desc_t;

typedef struct e1000 {
    volatile uint8_t *regs;
    netdev_t         *ndev;

    e1000_rx_desc_t  *rx;   uintptr_t rx_phys;
    e1000_tx_desc_t  *tx;   uintptr_t tx_phys;
    uint8_t          *rx_buf[E1000_NUM_RX];
    uint8_t          *tx_buf[E1000_NUM_TX]; uintptr_t tx_buf_phys[E1000_NUM_TX];

    uint32_t          rx_cur;
    uint32_t          tx_cur;

    int               vector;
    int               polled;
    spinlock_t        lock;

    struct e1000     *next;
} e1000_t;

static e1000_t *g_nics;

static inline uint32_t er(e1000_t *e, uint32_t off) {
    return *(volatile uint32_t *)(e->regs + off);
}
static inline void ew(e1000_t *e, uint32_t off, uint32_t v) {
    *(volatile uint32_t *)(e->regs + off) = v;
}

static uint16_t eeprom_read(e1000_t *e, uint8_t addr) {
    ew(e, E1000_EERD, ((uint32_t)addr << 8) | 1u);
    uint32_t v = 0;
    for (int i = 0; i < 100000; i++) {
        v = er(e, E1000_EERD);
        if (v & (1u << 4)) break;
    }
    return (uint16_t)(v >> 16);
}

static void read_mac(e1000_t *e, uint8_t mac[6]) {
    uint16_t w0 = eeprom_read(e, 0), w1 = eeprom_read(e, 1), w2 = eeprom_read(e, 2);
    mac[0] = (uint8_t)w0; mac[1] = (uint8_t)(w0 >> 8);
    mac[2] = (uint8_t)w1; mac[3] = (uint8_t)(w1 >> 8);
    mac[4] = (uint8_t)w2; mac[5] = (uint8_t)(w2 >> 8);

    if (!(mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5])) {
        uint32_t ral = er(e, E1000_RAL), rah = er(e, E1000_RAH);
        mac[0] = (uint8_t)ral; mac[1] = (uint8_t)(ral >> 8);
        mac[2] = (uint8_t)(ral >> 16); mac[3] = (uint8_t)(ral >> 24);
        mac[4] = (uint8_t)rah; mac[5] = (uint8_t)(rah >> 8);
    }
}

static void e1000_rx_drain(e1000_t *e) {
    uint8_t buf[E1000_BUFSZ];
    for (;;) {
        uint64_t f = spinlock_acquire_irqsave(&e->lock);
        uint32_t i = e->rx_cur;
        if (!(e->rx[i].status & RXD_STAT_DD)) {
            spinlock_release_irqrestore(&e->lock, f);
            return;
        }
        uint16_t len = e->rx[i].length;
        if (len > E1000_BUFSZ) len = E1000_BUFSZ;
        memcpy(buf, e->rx_buf[i], len);
        e->rx[i].status = 0;
        ew(e, E1000_RDT, i);
        e->rx_cur = (i + 1) % E1000_NUM_RX;
        spinlock_release_irqrestore(&e->lock, f);

        net_rx(e->ndev, buf, len);
    }
}

static int e1000_transmit(netdev_t *nd, const void *frame, size_t len) {
    e1000_t *e = nd->priv;
    if (len == 0 || len > E1000_BUFSZ) return -1;

    uint64_t f = spinlock_acquire_irqsave(&e->lock);
    uint32_t i = e->tx_cur;
    if (!(e->tx[i].status & TXD_STAT_DD) && e->tx[i].cmd) {
        spinlock_release_irqrestore(&e->lock, f);
        return -1;
    }
    memcpy(e->tx_buf[i], frame, len);
    e->tx[i].addr   = e->tx_buf_phys[i];
    e->tx[i].length = (uint16_t)len;
    e->tx[i].cso    = 0;
    e->tx[i].cmd    = TXD_CMD_EOP | TXD_CMD_IFCS | TXD_CMD_RS;
    e->tx[i].status = 0;
    e->tx_cur = (i + 1) % E1000_NUM_TX;
    ew(e, E1000_TDT, e->tx_cur);
    spinlock_release_irqrestore(&e->lock, f);

    for (int t = 0; t < 1000000; t++) {
        if (e->tx[i].status & TXD_STAT_DD) break;
    }
    return 0;
}

static void e1000_irq(void *ctx) {
    e1000_t *e = ctx;
    (void)er(e, E1000_ICR);
    e1000_rx_drain(e);
}

static void e1000_worker(void *arg) {
    (void)arg;
    for (;;) {
        for (e1000_t *e = g_nics; e; e = e->next)
            if (e->polled) e1000_rx_drain(e);
        task_sleep_ms(1);
    }
}

static int e1000_probe(pci_device_t *dev) {
    if (dev->bars[0].type != PCI_BAR_TYPE_MEM || !dev->bars[0].base) {
        serial_printf("[e1000] BAR0 not MMIO, skipping\n");
        return -1;
    }

    uint16_t cmd = pci_config_read16(dev->segment, dev->bus, dev->device, dev->function, PCI_COMMAND);
    cmd |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
    pci_config_write16(dev->segment, dev->bus, dev->device, dev->function, PCI_COMMAND, cmd);

    e1000_t *e = calloc(1, sizeof(*e));
    if (!e) return -1;
    e->regs = (volatile uint8_t *)mmio_map(dev->bars[0].base, dev->bars[0].size);
    if (!e->regs) { free(e); return -1; }

    ew(e, E1000_IMC, 0xFFFFFFFFu);
    ew(e, E1000_CTRL, er(e, E1000_CTRL) | CTRL_RST);
    for (volatile int d = 0; d < 1000000; d++) { }
    ew(e, E1000_IMC, 0xFFFFFFFFu);
    (void)er(e, E1000_ICR);

    ew(e, E1000_CTRL, er(e, E1000_CTRL) | CTRL_SLU | CTRL_ASDE);

    for (int i = 0; i < 128; i++) ew(e, E1000_MTA + i * 4, 0);

    uint8_t mac[6];
    read_mac(e, mac);
    ew(e, E1000_RAL, (uint32_t)mac[0] | ((uint32_t)mac[1] << 8) |
                     ((uint32_t)mac[2] << 16) | ((uint32_t)mac[3] << 24));
    ew(e, E1000_RAH, (uint32_t)mac[4] | ((uint32_t)mac[5] << 8) | (1u << 31));

    e->rx = dma_alloc_coherent_low(E1000_NUM_RX * sizeof(e1000_rx_desc_t), &e->rx_phys);
    if (!e->rx) { free(e); return -1; }
    memset(e->rx, 0, E1000_NUM_RX * sizeof(e1000_rx_desc_t));
    for (int i = 0; i < E1000_NUM_RX; i++) {
        uintptr_t bp;
        e->rx_buf[i] = dma_alloc_coherent_low(E1000_BUFSZ, &bp);
        e->rx[i].addr = bp;
        e->rx[i].status = 0;
    }
    ew(e, E1000_RDBAL, (uint32_t)(e->rx_phys & 0xFFFFFFFFu));
    ew(e, E1000_RDBAH, (uint32_t)(e->rx_phys >> 32));
    ew(e, E1000_RDLEN, E1000_NUM_RX * sizeof(e1000_rx_desc_t));
    ew(e, E1000_RDH, 0);
    ew(e, E1000_RDT, E1000_NUM_RX - 1);
    ew(e, E1000_RCTL, RCTL_EN | RCTL_BAM | RCTL_SECRC);

    e->tx = dma_alloc_coherent_low(E1000_NUM_TX * sizeof(e1000_tx_desc_t), &e->tx_phys);
    if (!e->tx) { free(e); return -1; }
    memset(e->tx, 0, E1000_NUM_TX * sizeof(e1000_tx_desc_t));
    for (int i = 0; i < E1000_NUM_TX; i++) {
        e->tx_buf[i] = dma_alloc_coherent_low(E1000_BUFSZ, &e->tx_buf_phys[i]);
        e->tx[i].status = TXD_STAT_DD;
    }
    ew(e, E1000_TDBAL, (uint32_t)(e->tx_phys & 0xFFFFFFFFu));
    ew(e, E1000_TDBAH, (uint32_t)(e->tx_phys >> 32));
    ew(e, E1000_TDLEN, E1000_NUM_TX * sizeof(e1000_tx_desc_t));
    ew(e, E1000_TDH, 0);
    ew(e, E1000_TDT, 0);
    ew(e, E1000_TCTL, TCTL_EN | TCTL_PSP | (0x10u << TCTL_CT_SHIFT) | (0x40u << TCTL_COLD_SHIFT));
    ew(e, E1000_TIPG, 10u | (8u << 10) | (6u << 20));

    e->ndev = netdev_register(mac, 1500, e1000_transmit, e);
    if (!e->ndev) { free(e); return -1; }
    e->ndev->link_up = (er(e, E1000_STATUS) & STATUS_LU) ? 1 : 0;

    e->next = g_nics;
    g_nics = e;

    int vec = irq_alloc_vector();
    if (vec > 0 && dev->cap_msi_off &&
        pci_enable_msi(dev, (uint8_t)vec, lapic_get_id()) == 0 &&
        irq_request(vec, e1000_irq, e, "e1000") == 0) {
        e->vector = vec;
        (void)er(e, E1000_ICR);
        ew(e, E1000_IMS, IM_RXT0 | IM_RXO | IM_RXDMT0 | IM_LSC);
    } else {
        if (vec > 0) irq_free_vector(vec);
        e->polled = 1;
    }

    serial_printf("[e1000] %s: MAC %02x:%02x:%02x:%02x:%02x:%02x link=%s irq=%s\n",
                  e->ndev->name, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  e->ndev->link_up ? "up" : "down",
                  e->polled ? "poll" : "msi");
    return 0;
}

static const pci_driver_t g_e1000_driver = {
    .name           = "e1000",
    .match_vendor   = 0x8086,
    .match_device   = -1,
    .match_class    = 0x02,
    .match_subclass = 0x00,
    .probe          = e1000_probe,
};

void e1000_init(void) {
    pci_register_driver(&g_e1000_driver);
}

void e1000_start_worker(void) {
    for (e1000_t *e = g_nics; e; e = e->next)
        if (e->polled) {
            task_create("e1000_worker", e1000_worker, NULL, 1);
            return;
        }
}
