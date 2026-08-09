#include "../../include/net/netdev.h"
#include "../../include/net/net.h"
#include "../../include/net/arp.h"
#include "../../include/net/ip.h"
#include "../../include/net/icmp.h"
#include "../../include/net/udp.h"
#include "../../include/net/tcp.h"
#include "../../include/net/dhcp.h"
#include "../../include/net/dns.h"
#include "../../include/drivers/net/e1000.h"
#include "../../include/drivers/net/rtl8139.h"
#include "../../include/drivers/net/rtl8169.h"
#include "../../include/drivers/net/virtio_net.h"
#include "../../include/drivers/net/ne2000.h"
#include "../../include/sched/sched.h"
#include "../../include/io/serial.h"
#include <string.h>
#include <stdlib.h>

extern uint64_t sched_now_ns(void);

const uint8_t eth_broadcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

static netdev_t *g_netdevs;
static int       g_eth_index;

netdev_t *netdev_register(const uint8_t mac[ETH_ALEN], uint32_t mtu,
                          int (*transmit)(netdev_t *, const void *, size_t),
                          void *priv) {
    netdev_t *d = calloc(1, sizeof(*d));
    if (!d) return NULL;

    d->name[0] = 'e'; d->name[1] = 't'; d->name[2] = 'h';
    d->name[3] = (char)('0' + (g_eth_index++ % 10));
    d->name[4] = '\0';

    memcpy(d->mac, mac, ETH_ALEN);
    d->mtu = mtu;
    d->transmit = transmit;
    d->priv = priv;

    d->ip6_ll[0] = 0xfe; d->ip6_ll[1] = 0x80;
    d->ip6_ll[8] = mac[0] ^ 0x02; d->ip6_ll[9] = mac[1]; d->ip6_ll[10] = mac[2];
    d->ip6_ll[11] = 0xff; d->ip6_ll[12] = 0xfe;
    d->ip6_ll[13] = mac[3]; d->ip6_ll[14] = mac[4]; d->ip6_ll[15] = mac[5];

    d->next = g_netdevs;
    g_netdevs = d;
    return d;
}

netdev_t *netdev_first(void) { return g_netdevs; }

netdev_t *netdev_get(const char *name) {
    for (netdev_t *d = g_netdevs; d; d = d->next)
        if (strcmp(d->name, name) == 0) return d;
    return NULL;
}

int netdev_transmit(netdev_t *dev, const void *frame, size_t len) {
    if (!dev || !dev->transmit) return -1;
    int r = dev->transmit(dev, frame, len);
    if (r == 0) { dev->tx_packets++; dev->tx_bytes += len; }
    else         dev->tx_dropped++;
    return r;
}

int eth_send(netdev_t *dev, const uint8_t dst[6], uint16_t ethertype,
             const void *payload, size_t len) {
    if (!dev || len > dev->mtu) return -1;
    uint8_t frame[1600];
    memcpy(frame, dst, 6);
    memcpy(frame + 6, dev->mac, 6);
    wr16be(frame + 12, ethertype);
    memcpy(frame + 14, payload, len);
    size_t total = 14 + len;
    if (total < 60) { memset(frame + total, 0, 60 - total); total = 60; }
    return netdev_transmit(dev, frame, total);
}

void net_rx(netdev_t *dev, const void *frame, size_t len) {
    dev->rx_packets++;
    dev->rx_bytes += len;
    if (len < 14) return;

    const uint8_t *p = frame;
    uint16_t ethertype = rd16be(p + 12);
    switch (ethertype) {
        case ETH_P_ARP:
            arp_rx(dev, p + 14, len - 14);
            break;
        case ETH_P_IP:
            ip_rx(dev, p + 14, len - 14);
            break;
        case ETH_P_IPV6: {
            extern void ipv6_rx(netdev_t *dev, const uint8_t *smac, const uint8_t *pkt, size_t len);
            ipv6_rx(dev, p + 6, p + 14, len - 14);
            break;
        }
        default:
            break;
    }
}

static void net_worker(void *arg) {
    (void)arg;
    netdev_t *dev = netdev_first();
    if (!dev) { for (;;) task_sleep_ms(1000); }

    dhcp_start(dev);

    int tick = 0;
    uint64_t next_tick = 0;
    for (;;) {
        int did = 0, spins = 0;
        extern int ipv6_loopback_drain_one(void);
        while ((loopback_drain_one() | ipv6_loopback_drain_one()) && ++spins < 8192) did = 1;

        uint64_t now = sched_now_ns();
        if (now >= next_tick) {
            tcp_tick();
            if (++tick % 4 == 0) {
                if (!dhcp_bound()) dhcp_start(dev);
                arp_age();
            }
            next_tick = now + 250000000ULL;
        }
        task_sleep_ms(did ? 2 : 60);
    }
}

int net_ifcfg_get(int index, net_ifcfg_t *out) {
    netdev_t *d = g_netdevs;
    for (int i = 0; d && i < index; i++) d = d->next;
    if (!d) return -1;
    memset(out, 0, sizeof(*out));
    memcpy(out->name, d->name, NETDEV_NAME_MAX);
    memcpy(out->mac, d->mac, 6);
    out->ip = d->ip; out->netmask = d->netmask; out->gateway = d->gateway; out->dns = d->dns;
    out->rx_packets = d->rx_packets; out->tx_packets = d->tx_packets;
    out->rx_bytes = d->rx_bytes; out->tx_bytes = d->tx_bytes;
    out->link_up = d->link_up;
    out->mtu = (int32_t)d->mtu;
    memcpy(out->ip6_ll, d->ip6_ll, 16);
    return 0;
}

int net_ifcfg_set(int index, uint32_t ip, uint32_t netmask, uint32_t gateway, uint32_t dns) {
    netdev_t *d = g_netdevs;
    for (int i = 0; d && i < index; i++) d = d->next;
    if (!d) return -1;
    d->ip = ip;
    if (netmask) d->netmask = netmask;
    if (gateway == 0xFFFFFFFFu) d->gateway = 0; else if (gateway) d->gateway = gateway;
    if (dns == 0xFFFFFFFFu) d->dns = 0; else if (dns) d->dns = dns;
    return 0;
}

void net_start_worker(void) {
    task_create("net", net_worker, NULL, 1);
}

void net_init(void) {
    e1000_init();
    rtl8139_init();
    rtl8169_init();
    virtio_net_init();
    cinder_ne2000_init();
}
