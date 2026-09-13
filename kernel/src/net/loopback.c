#include "../../include/net/netdev.h"
#include "../../include/net/net.h"
#include "../../include/net/ip.h"
#include "../../include/sched/spinlock.h"
#include <string.h>
#include <stdlib.h>

#define LO_MTU     65535
#define LO_BUDGET  (512 * 1024)

typedef struct lo_pkt {
    struct lo_pkt *next;
    uint16_t       ethertype;
    size_t         len;
    uint8_t        data[];
} lo_pkt_t;

static netdev_t  *g_lo;
static lo_pkt_t  *g_head, *g_tail;
static size_t     g_queued;
static volatile int g_draining;
static spinlock_t g_lock = SPINLOCK_INIT;

static int lo_enqueue(uint16_t ethertype, const uint8_t *pkt, size_t len)
{
    if (!len || len > LO_MTU) return -1;

    lo_pkt_t *e = malloc(sizeof(*e) + len);
    if (!e) return -1;
    e->next = NULL;
    e->ethertype = ethertype;
    e->len = len;
    memcpy(e->data, pkt, len);

    uint64_t f = spinlock_acquire_irqsave(&g_lock);
    if (g_queued + len > LO_BUDGET) {
        spinlock_release_irqrestore(&g_lock, f);
        free(e);
        if (g_lo) g_lo->tx_dropped++;
        return -1;
    }
    if (g_tail) g_tail->next = e; else g_head = e;
    g_tail = e;
    g_queued += len;
    spinlock_release_irqrestore(&g_lock, f);
    return 0;
}

static int lo_transmit(netdev_t *dev, const void *frame, size_t len)
{
    (void)dev;
    const uint8_t *p = frame;
    if (len < 1) return -1;
    return lo_enqueue((p[0] >> 4) == 6 ? ETH_P_IPV6 : ETH_P_IP, p, len);
}

int loopback_output(uint16_t ethertype, const uint8_t *pkt, size_t len)
{
    if (!g_lo) return -1;
    if (lo_enqueue(ethertype, pkt, len) != 0) return -1;
    g_lo->tx_packets++;
    g_lo->tx_bytes += len;
    return 0;
}

int loopback_drain_one(void)
{
    uint64_t f = spinlock_acquire_irqsave(&g_lock);
    lo_pkt_t *e = g_head;
    if (e) {
        g_head = e->next;
        if (!g_head) g_tail = NULL;
        g_queued -= e->len;
    }
    spinlock_release_irqrestore(&g_lock, f);
    if (!e) return 0;

    g_lo->rx_packets++;
    g_lo->rx_bytes += e->len;
    if (e->ethertype == ETH_P_IPV6) {
        extern void ipv6_rx(netdev_t *dev, const uint8_t *smac, const uint8_t *pkt, size_t len);
        g_lo->rx_other++;
        ipv6_rx(g_lo, g_lo->mac, e->data, e->len);
    } else {
        g_lo->rx_ip++;
        ip_rx(g_lo, e->data, e->len);
    }
    free(e);
    return 1;
}

void loopback_drain_all(void)
{
    if (__atomic_exchange_n(&g_draining, 1, __ATOMIC_ACQUIRE)) return;
    int spins = 0;
    while (loopback_drain_one() && ++spins < 256) { }
    __atomic_store_n(&g_draining, 0, __ATOMIC_RELEASE);
}

void loopback_init(void)
{
    static const uint8_t zero_mac[6] = { 0, 0, 0, 0, 0, 0 };
    if (g_lo) return;
    g_lo = netdev_register_named("lo", zero_mac, LO_MTU, lo_transmit, NULL);
    if (!g_lo) return;
    g_lo->is_loopback = 1;
    g_lo->link_up     = 1;
    g_lo->ip          = IP4(127, 0, 0, 1);
    g_lo->netmask     = IP4(255, 0, 0, 0);
    memset(g_lo->ip6_ll, 0, 16);
    g_lo->ip6_ll[15] = 1;
}
