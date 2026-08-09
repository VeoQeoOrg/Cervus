#include "../../../include/drivers/net/virtio_net.h"
#include "../../../include/net/netdev.h"
#include "../../../include/drivers/pci.h"
#include "../../../include/memory/dma.h"
#include "../../../include/sched/sched.h"
#include "../../../include/sched/spinlock.h"
#include "../../../include/io/ports.h"
#include "../../../include/io/serial.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#define VIRTIO_HOST_FEATURES   0x00
#define VIRTIO_GUEST_FEATURES  0x04
#define VIRTIO_QUEUE_PFN       0x08
#define VIRTIO_QUEUE_NUM       0x0C
#define VIRTIO_QUEUE_SEL       0x0E
#define VIRTIO_QUEUE_NOTIFY    0x10
#define VIRTIO_STATUS          0x12
#define VIRTIO_ISR             0x13
#define VIRTIO_CONFIG          0x14

#define VS_ACK       0x01
#define VS_DRIVER    0x02
#define VS_DRIVER_OK 0x04
#define VS_FAILED    0x80

#define VNET_F_MAC   5

#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2

#define VNET_HDR_LEN 10
#define VNET_BUFSZ   2048

#define RXQ 0
#define TXQ 1

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} vq_desc_t;

typedef struct __attribute__((packed)) {
    uint32_t id;
    uint32_t len;
} vq_used_elem_t;

typedef struct {
    int                num;
    void              *mem;
    volatile vq_desc_t *desc;
    volatile uint16_t *avail_flags;
    volatile uint16_t *avail_idx;
    volatile uint16_t *avail_ring;
    volatile uint16_t *used_flags;
    volatile uint16_t *used_idx;
    volatile vq_used_elem_t *used_ring;
    uint16_t           last_used;
    uint8_t           *bufmem;
    uintptr_t          bufphys;
} vq_t;

typedef struct virtio_net {
    uint16_t   io;
    netdev_t  *ndev;
    vq_t       rx;
    vq_t       tx;
    uint16_t   tx_next;
    spinlock_t tx_lock;
    struct virtio_net *next;
} virtio_net_t;

static virtio_net_t *g_nics;

static inline void mb(void) { asm volatile("mfence" ::: "memory"); }

static int vq_setup(virtio_net_t *v, int qidx, vq_t *vq) {
    outw(v->io + VIRTIO_QUEUE_SEL, (uint16_t)qidx);
    uint16_t num = inw(v->io + VIRTIO_QUEUE_NUM);
    if (num == 0) return -1;
    vq->num = num;

    size_t desc_sz  = (size_t)16 * num;
    size_t avail_sz = 6 + (size_t)2 * num;
    size_t used_off = (desc_sz + avail_sz + 4095) & ~((size_t)4095);
    size_t total    = used_off + 6 + (size_t)8 * num;

    uintptr_t phys;
    void *mem = dma_alloc_coherent_low(total, &phys);
    if (!mem) return -1;
    memset(mem, 0, total);
    vq->mem = mem;

    uint8_t *base = mem;
    vq->desc        = (volatile vq_desc_t *)base;
    vq->avail_flags = (volatile uint16_t *)(base + desc_sz);
    vq->avail_idx   = (volatile uint16_t *)(base + desc_sz + 2);
    vq->avail_ring  = (volatile uint16_t *)(base + desc_sz + 4);
    vq->used_flags  = (volatile uint16_t *)(base + used_off);
    vq->used_idx    = (volatile uint16_t *)(base + used_off + 2);
    vq->used_ring   = (volatile vq_used_elem_t *)(base + used_off + 4);
    vq->last_used   = 0;

    vq->bufmem = dma_alloc_coherent_low((size_t)num * VNET_BUFSZ, &vq->bufphys);
    if (!vq->bufmem) return -1;

    outl(v->io + VIRTIO_QUEUE_PFN, (uint32_t)(phys >> 12));
    return 0;
}

static void vq_notify(virtio_net_t *v, int qidx) {
    outw(v->io + VIRTIO_QUEUE_NOTIFY, (uint16_t)qidx);
}

static void rx_refill_all(virtio_net_t *v) {
    vq_t *q = &v->rx;
    for (int i = 0; i < q->num; i++) {
        q->desc[i].addr  = q->bufphys + (uint64_t)i * VNET_BUFSZ;
        q->desc[i].len   = VNET_BUFSZ;
        q->desc[i].flags = VRING_DESC_F_WRITE;
        q->desc[i].next  = 0;
        q->avail_ring[i] = (uint16_t)i;
    }
    mb();
    *q->avail_idx = (uint16_t)q->num;
    mb();
    vq_notify(v, RXQ);
}

static void virtio_rx_drain(virtio_net_t *v) {
    vq_t *q = &v->rx;
    while (q->last_used != *q->used_idx) {
        mb();
        vq_used_elem_t e = q->used_ring[q->last_used % q->num];
        uint32_t id = e.id % (uint32_t)q->num;
        uint32_t len = e.len;
        if (len > VNET_HDR_LEN && len <= VNET_BUFSZ) {
            uint8_t *buf = q->bufmem + (size_t)id * VNET_BUFSZ;
            net_rx(v->ndev, buf + VNET_HDR_LEN, len - VNET_HDR_LEN);
        }
        q->desc[id].addr  = q->bufphys + (uint64_t)id * VNET_BUFSZ;
        q->desc[id].len   = VNET_BUFSZ;
        q->desc[id].flags = VRING_DESC_F_WRITE;
        q->avail_ring[*q->avail_idx % q->num] = (uint16_t)id;
        mb();
        (*q->avail_idx)++;
        q->last_used++;
    }
    mb();
    vq_notify(v, RXQ);
}

static int virtio_transmit(netdev_t *nd, const void *frame, size_t len) {
    virtio_net_t *v = nd->priv;
    if (len == 0 || len + VNET_HDR_LEN > VNET_BUFSZ) return -1;
    vq_t *q = &v->tx;

    spinlock_acquire(&v->tx_lock);
    uint16_t id = v->tx_next;
    v->tx_next = (uint16_t)((v->tx_next + 1) % q->num);

    uint8_t *b = q->bufmem + (size_t)id * VNET_BUFSZ;
    memset(b, 0, VNET_HDR_LEN);
    memcpy(b + VNET_HDR_LEN, frame, len);

    q->desc[id].addr  = q->bufphys + (uint64_t)id * VNET_BUFSZ;
    q->desc[id].len   = (uint32_t)(VNET_HDR_LEN + len);
    q->desc[id].flags = 0;
    q->desc[id].next  = 0;

    uint16_t old_used = *q->used_idx;
    q->avail_ring[*q->avail_idx % q->num] = id;
    mb();
    (*q->avail_idx)++;
    mb();
    vq_notify(v, TXQ);

    for (int t = 0; t < 2000000; t++) {
        if (*q->used_idx != old_used) break;
    }
    q->last_used = *q->used_idx;
    spinlock_release(&v->tx_lock);
    return 0;
}

static void virtio_worker(void *arg) {
    (void)arg;
    for (;;) {
        for (virtio_net_t *v = g_nics; v; v = v->next)
            virtio_rx_drain(v);
        task_sleep_ms(1);
    }
}

static int virtio_probe(pci_device_t *dev) {
    if (dev->device_id != 0x1000 && dev->device_id != 0x1041) return -1;

    int barx = -1;
    for (int i = 0; i < 6; i++)
        if (dev->bars[i].type == PCI_BAR_TYPE_IO && dev->bars[i].base) { barx = i; break; }
    if (barx < 0) { serial_printf("[virtio-net] %04x: no I/O BAR (modern-only, skipping)\n", dev->device_id); return -1; }

    uint16_t cmd = pci_config_read16(dev->segment, dev->bus, dev->device, dev->function, PCI_COMMAND);
    cmd |= PCI_COMMAND_IO | PCI_COMMAND_MASTER;
    pci_config_write16(dev->segment, dev->bus, dev->device, dev->function, PCI_COMMAND, cmd);

    virtio_net_t *v = calloc(1, sizeof(*v));
    if (!v) return -1;
    v->io = (uint16_t)dev->bars[barx].base;

    outb(v->io + VIRTIO_STATUS, 0);
    outb(v->io + VIRTIO_STATUS, VS_ACK);
    outb(v->io + VIRTIO_STATUS, VS_ACK | VS_DRIVER);

    uint32_t host = inl(v->io + VIRTIO_HOST_FEATURES);
    uint32_t guest = host & (1u << VNET_F_MAC);
    outl(v->io + VIRTIO_GUEST_FEATURES, guest);

    uint8_t mac[6];
    if (host & (1u << VNET_F_MAC)) {
        for (int i = 0; i < 6; i++) mac[i] = inb(v->io + VIRTIO_CONFIG + i);
    } else {
        mac[0] = 0x52; mac[1] = 0x54; mac[2] = 0x00;
        mac[3] = 0x12; mac[4] = 0x34; mac[5] = (uint8_t)(0x56 ^ dev->device);
    }

    if (vq_setup(v, RXQ, &v->rx) != 0 || vq_setup(v, TXQ, &v->tx) != 0) {
        outb(v->io + VIRTIO_STATUS, VS_FAILED);
        free(v);
        return -1;
    }
    rx_refill_all(v);

    v->ndev = netdev_register(mac, 1500, virtio_transmit, v);
    if (!v->ndev) { outb(v->io + VIRTIO_STATUS, VS_FAILED); free(v); return -1; }
    v->ndev->link_up = 1;

    outb(v->io + VIRTIO_STATUS, VS_ACK | VS_DRIVER | VS_DRIVER_OK);

    v->next = g_nics;
    g_nics = v;

    serial_printf("[virtio-net] %s: dev=%04x MAC %02x:%02x:%02x:%02x:%02x:%02x rxq=%d txq=%d\n",
                  v->ndev->name, dev->device_id,
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], v->rx.num, v->tx.num);
    return 0;
}

static const pci_driver_t g_virtio_net_driver = {
    .name           = "virtio-net",
    .match_vendor   = 0x1AF4,
    .match_device   = -1,
    .match_class    = 0x02,
    .match_subclass = 0x00,
    .probe          = virtio_probe,
};

void virtio_net_init(void) {
    pci_register_driver(&g_virtio_net_driver);
}

void virtio_net_start_worker(void) {
    if (g_nics) task_create("virtio_net", virtio_worker, NULL, 1);
}
