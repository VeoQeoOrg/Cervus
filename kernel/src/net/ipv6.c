#include "../../include/net/netdev.h"
#include "../../include/net/net.h"
#include "../../include/net/ipv6.h"
#include "../../include/sched/spinlock.h"
#include "../../include/io/serial.h"
#include <string.h>

#define IPPROTO_TCP     6
#define IPPROTO_UDP    17
#define IPPROTO_ICMPV6 58
#define ND_NS 135
#define ND_NA 136
#define ICMP6_ECHO_REQ 128
#define ICMP6_ECHO_REP 129

extern void udp6_rx(netdev_t *dev, const uint8_t *src, const uint8_t *dst, const uint8_t *p, size_t len);
extern void tcp6_rx(netdev_t *dev, const uint8_t *src6, const uint8_t *dst6, const uint8_t *seg, size_t len);

static const uint8_t ip6_lo[16] = { 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,1 };

static int is_lo6(const uint8_t *a) { return memcmp(a, ip6_lo, 16) == 0; }
static int is_mcast6(const uint8_t *a) { return a[0] == 0xff; }

#define ND_CACHE 32
static struct { uint8_t ip[16]; uint8_t mac[6]; int valid; } g_nd[ND_CACHE];
static spinlock_t g_nd_lock = SPINLOCK_INIT;

static void nd_learn(const uint8_t *ip, const uint8_t *mac) {
    if (is_mcast6(ip) || is_lo6(ip)) return;
    uint64_t f = spinlock_acquire_irqsave(&g_nd_lock);
    int free = -1;
    for (int i = 0; i < ND_CACHE; i++) {
        if (g_nd[i].valid && memcmp(g_nd[i].ip, ip, 16) == 0) { memcpy(g_nd[i].mac, mac, 6); spinlock_release_irqrestore(&g_nd_lock, f); return; }
        if (!g_nd[i].valid && free < 0) free = i;
    }
    if (free < 0) free = 0;
    memcpy(g_nd[free].ip, ip, 16); memcpy(g_nd[free].mac, mac, 6); g_nd[free].valid = 1;
    spinlock_release_irqrestore(&g_nd_lock, f);
}
static int nd_lookup(const uint8_t *ip, uint8_t *mac) {
    uint64_t f = spinlock_acquire_irqsave(&g_nd_lock);
    for (int i = 0; i < ND_CACHE; i++) if (g_nd[i].valid && memcmp(g_nd[i].ip, ip, 16) == 0) { memcpy(mac, g_nd[i].mac, 6); spinlock_release_irqrestore(&g_nd_lock, f); return 0; }
    spinlock_release_irqrestore(&g_nd_lock, f);
    return -1;
}

#define LB6_Q 32
typedef struct { netdev_t *dev; size_t len; uint8_t data[1500]; } lb6_pkt_t;
static lb6_pkt_t g_lb6[LB6_Q];
static int g_lb6_h, g_lb6_t, g_lb6_c;
static spinlock_t g_lb6_lock = SPINLOCK_INIT;

static void lb6_enqueue(netdev_t *dev, const uint8_t *pkt, size_t len) {
    if (len > 1500) return;
    uint64_t f = spinlock_acquire_irqsave(&g_lb6_lock);
    if (g_lb6_c < LB6_Q) { g_lb6[g_lb6_t].dev = dev; g_lb6[g_lb6_t].len = len; memcpy(g_lb6[g_lb6_t].data, pkt, len); g_lb6_t = (g_lb6_t + 1) % LB6_Q; g_lb6_c++; }
    spinlock_release_irqrestore(&g_lb6_lock, f);
}

static uint16_t icmp6_cksum(const uint8_t *src, const uint8_t *dst, const uint8_t *msg, uint32_t len) {
    uint32_t sum = 0;
    for (int i = 0; i < 16; i += 2) sum += (uint32_t)((src[i] << 8) | src[i + 1]);
    for (int i = 0; i < 16; i += 2) sum += (uint32_t)((dst[i] << 8) | dst[i + 1]);
    sum += (len >> 16) & 0xffff; sum += len & 0xffff;
    sum += IPPROTO_ICMPV6;
    for (uint32_t i = 0; i + 1 < len; i += 2) sum += (uint32_t)((msg[i] << 8) | msg[i + 1]);
    if (len & 1) sum += (uint32_t)(msg[len - 1] << 8);
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}

int ipv6_output(netdev_t *dev, const uint8_t *src_in, const uint8_t *dst, uint8_t nexthdr, const uint8_t *payload, uint32_t plen) {
    if (plen > 1500 - 40) return -1;
    uint8_t src[16];
    if (src_in) memcpy(src, src_in, 16); else memcpy(src, dev->ip6_ll, 16);

    uint8_t pkt[1500];
    memset(pkt, 0, 40);
    pkt[0] = 0x60;
    pkt[4] = (uint8_t)(plen >> 8); pkt[5] = (uint8_t)plen;
    pkt[6] = nexthdr;
    pkt[7] = 255;
    memcpy(pkt + 8, src, 16);
    memcpy(pkt + 24, dst, 16);
    memcpy(pkt + 40, payload, plen);

    if (is_lo6(dst) || memcmp(dst, dev->ip6_ll, 16) == 0) {
        lb6_enqueue(dev, pkt, 40 + plen);
        return 0;
    }

    uint8_t mac[6];
    if (is_mcast6(dst)) { mac[0] = 0x33; mac[1] = 0x33; mac[2] = dst[12]; mac[3] = dst[13]; mac[4] = dst[14]; mac[5] = dst[15]; }
    else if (nd_lookup(dst, mac) != 0) { ipv6_resolve(dev, dst); return -1; }

    return eth_send(dev, mac, ETH_P_IPV6, pkt, 40 + plen);
}

void ipv6_resolve(netdev_t *dev, const uint8_t *target) {
    uint8_t sol[16] = { 0xff,0x02,0,0,0,0,0,0, 0,0,0,1,0xff, target[13], target[14], target[15] };
    uint8_t msg[32];
    memset(msg, 0, sizeof msg);
    msg[0] = ND_NS;
    memcpy(msg + 8, target, 16);
    msg[24] = 1; msg[25] = 1;
    memcpy(msg + 26, dev->mac, 6);
    uint16_t ck = icmp6_cksum(dev->ip6_ll, sol, msg, sizeof msg);
    msg[2] = ck >> 8; msg[3] = ck & 0xff;
    ipv6_output(dev, dev->ip6_ll, sol, IPPROTO_ICMPV6, msg, sizeof msg);
}

static void send_na(netdev_t *dev, const uint8_t *dst_ip, const uint8_t *target) {
    uint8_t msg[32];
    memset(msg, 0, sizeof msg);
    msg[0] = ND_NA;
    msg[4] = 0x60;
    memcpy(msg + 8, target, 16);
    msg[24] = 2; msg[25] = 1;
    memcpy(msg + 26, dev->mac, 6);
    uint16_t ck = icmp6_cksum(dev->ip6_ll, dst_ip, msg, sizeof msg);
    msg[2] = ck >> 8; msg[3] = ck & 0xff;
    ipv6_output(dev, dev->ip6_ll, dst_ip, IPPROTO_ICMPV6, msg, sizeof msg);
}

static void icmp6_rx(netdev_t *dev, const uint8_t *src, const uint8_t *dst, const uint8_t *msg, uint32_t plen) {
    uint8_t type = msg[0];
    if (type == ND_NS) {
        if (plen < 24) return;
        const uint8_t *target = msg + 8;
        if (msg[24] == 1) nd_learn(src, msg + 26);
        if (memcmp(target, dev->ip6_ll, 16) == 0) send_na(dev, src, target);
    } else if (type == ND_NA) {
        if (plen < 24) return;
        if (plen >= 32 && msg[24] == 2) nd_learn(msg + 8, msg + 26);
    } else if (type == ICMP6_ECHO_REQ) {
        if (memcmp(dst, dev->ip6_ll, 16) != 0 && !is_lo6(dst)) return;
        uint8_t reply[1452];
        if (plen > sizeof reply) return;
        memcpy(reply, msg, plen);
        reply[0] = ICMP6_ECHO_REP;
        reply[2] = 0; reply[3] = 0;
        uint16_t ck = icmp6_cksum(dst, src, reply, plen);
        reply[2] = ck >> 8; reply[3] = ck & 0xff;
        ipv6_output(dev, dst, src, IPPROTO_ICMPV6, reply, plen);
    }
}

void ipv6_rx(netdev_t *dev, const uint8_t *smac, const uint8_t *pkt, size_t len) {
    if (len < 40 || (pkt[0] >> 4) != 6) return;
    uint16_t plen = (uint16_t)((pkt[4] << 8) | pkt[5]);
    uint8_t nexthdr = pkt[6];
    const uint8_t *src = pkt + 8;
    const uint8_t *dst = pkt + 24;
    if (40u + plen > len) plen = (uint16_t)(len - 40);
    const uint8_t *msg = pkt + 40;

    if (smac) nd_learn(src, smac);

    if (nexthdr == IPPROTO_ICMPV6 && plen >= 8) icmp6_rx(dev, src, dst, msg, plen);
    else if (nexthdr == IPPROTO_UDP && plen >= 8) udp6_rx(dev, src, dst, msg, plen);
    else if (nexthdr == IPPROTO_TCP && plen >= 20) tcp6_rx(dev, src, dst, msg, plen);
}

int ipv6_loopback_drain_one(void) {
    lb6_pkt_t p;
    uint64_t f = spinlock_acquire_irqsave(&g_lb6_lock);
    if (g_lb6_c == 0) { spinlock_release_irqrestore(&g_lb6_lock, f); return 0; }
    p.dev = g_lb6[g_lb6_h].dev; p.len = g_lb6[g_lb6_h].len;
    memcpy(p.data, g_lb6[g_lb6_h].data, p.len);
    g_lb6_h = (g_lb6_h + 1) % LB6_Q; g_lb6_c--;
    spinlock_release_irqrestore(&g_lb6_lock, f);
    ipv6_rx(p.dev, 0, p.data, p.len);
    return 1;
}

void ipv6_get_lladdr(netdev_t *dev, uint8_t out[16]) { memcpy(out, dev->ip6_ll, 16); }
