#include "../../include/net/arp.h"
#include "../../include/net/net.h"
#include "../../include/sched/spinlock.h"
#include "../../include/drivers/timer.h"
#include "../../include/io/serial.h"
#include <string.h>

static uint64_t now_ms(void) { return sched_now_ns() / 1000000ull; }

#define ARP_CACHE_SIZE 32
#define ARP_TTL_MS     (5 * 60 * 1000)

#define ARP_HTYPE_ETH  1
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2

typedef struct {
    uint32_t ip;
    uint8_t  mac[6];
    int      valid;
    uint64_t ts_ms;
} arp_entry_t;

static arp_entry_t g_arp[ARP_CACHE_SIZE];
static spinlock_t  g_arp_lock;

static void arp_cache_put(uint32_t ip, const uint8_t mac[6]) {
    if (!ip) return;
    spinlock_acquire(&g_arp_lock);
    int free_slot = -1, oldest = 0;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp[i].valid && g_arp[i].ip == ip) {
            memcpy(g_arp[i].mac, mac, 6);
            g_arp[i].ts_ms = now_ms();
            spinlock_release(&g_arp_lock);
            return;
        }
        if (!g_arp[i].valid && free_slot < 0) free_slot = i;
        if (g_arp[i].ts_ms < g_arp[oldest].ts_ms) oldest = i;
    }
    int slot = (free_slot >= 0) ? free_slot : oldest;
    g_arp[slot].ip = ip;
    memcpy(g_arp[slot].mac, mac, 6);
    g_arp[slot].valid = 1;
    g_arp[slot].ts_ms = now_ms();
    spinlock_release(&g_arp_lock);
}

int arp_lookup(uint32_t ip, uint8_t mac_out[6]) {
    int found = -1;
    spinlock_acquire(&g_arp_lock);
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp[i].valid && g_arp[i].ip == ip) {
            memcpy(mac_out, g_arp[i].mac, 6);
            found = 0;
            break;
        }
    }
    spinlock_release(&g_arp_lock);
    return found;
}

void arp_age(void) {
    uint64_t now = now_ms();
    spinlock_acquire(&g_arp_lock);
    for (int i = 0; i < ARP_CACHE_SIZE; i++)
        if (g_arp[i].valid && now - g_arp[i].ts_ms > ARP_TTL_MS)
            g_arp[i].valid = 0;
    spinlock_release(&g_arp_lock);
}

static void arp_build(uint8_t *pkt, uint16_t op, netdev_t *dev,
                      const uint8_t tha[6], uint32_t tpa) {
    wr16be(pkt + 0, ARP_HTYPE_ETH);
    wr16be(pkt + 2, ETH_P_IP);
    pkt[4] = 6;
    pkt[5] = 4;
    wr16be(pkt + 6, op);
    memcpy(pkt + 8, dev->mac, 6);
    wr32be(pkt + 14, dev->ip);
    if (tha) memcpy(pkt + 18, tha, 6);
    else     memset(pkt + 18, 0, 6);
    wr32be(pkt + 24, tpa);
}

void arp_request(netdev_t *dev, uint32_t target_ip) {
    if (!dev->ip) return;
    uint8_t pkt[28];
    arp_build(pkt, ARP_OP_REQUEST, dev, NULL, target_ip);
    eth_send(dev, eth_broadcast, ETH_P_ARP, pkt, sizeof(pkt));
}

void arp_rx(netdev_t *dev, const uint8_t *p, size_t len) {
    if (len < 28) return;
    if (rd16be(p + 0) != ARP_HTYPE_ETH || rd16be(p + 2) != ETH_P_IP) return;
    if (p[4] != 6 || p[5] != 4) return;

    uint16_t op  = rd16be(p + 6);
    const uint8_t *sha = p + 8;
    uint32_t spa = rd32be(p + 14);
    uint32_t tpa = rd32be(p + 24);

    arp_cache_put(spa, sha);

    if (op == ARP_OP_REQUEST && dev->ip && tpa == dev->ip) {
        uint8_t reply[28];
        arp_build(reply, ARP_OP_REPLY, dev, sha, spa);
        eth_send(dev, sha, ETH_P_ARP, reply, sizeof(reply));
    } else if (op == ARP_OP_REPLY) {
        serial_printf("[arp] %s: %u.%u.%u.%u is at %02x:%02x:%02x:%02x:%02x:%02x\n",
                      dev->name,
                      (spa >> 24) & 0xff, (spa >> 16) & 0xff, (spa >> 8) & 0xff, spa & 0xff,
                      sha[0], sha[1], sha[2], sha[3], sha[4], sha[5]);
    }
}
