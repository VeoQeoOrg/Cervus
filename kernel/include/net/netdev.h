#ifndef _KERNEL_NET_NETDEV_H
#define _KERNEL_NET_NETDEV_H

#include <stdint.h>
#include <stddef.h>

#define NETDEV_NAME_MAX 16
#define ETH_ALEN        6

typedef struct netdev netdev_t;

struct netdev {
    char     name[NETDEV_NAME_MAX];
    uint8_t  mac[ETH_ALEN];
    uint32_t mtu;
    int      link_up;

    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns;

    uint8_t  ip6_ll[16];

    void    *priv;
    int    (*transmit)(netdev_t *dev, const void *frame, size_t len);

    uint64_t rx_packets, rx_bytes, tx_packets, tx_bytes, rx_dropped, tx_dropped;

    netdev_t *next;
};

netdev_t *netdev_register(const uint8_t mac[ETH_ALEN], uint32_t mtu,
                          int (*transmit)(netdev_t *, const void *, size_t),
                          void *priv);
netdev_t *netdev_first(void);
netdev_t *netdev_get(const char *name);
int       netdev_transmit(netdev_t *dev, const void *frame, size_t len);

void net_rx(netdev_t *dev, const void *frame, size_t len);

void net_init(void);
void net_start_worker(void);

typedef struct {
    char     name[NETDEV_NAME_MAX];
    uint8_t  mac[6];
    uint8_t  _pad[2];
    uint32_t ip, netmask, gateway, dns;
    uint64_t rx_packets, tx_packets, rx_bytes, tx_bytes;
    int32_t  link_up;
    int32_t  mtu;
    uint8_t  ip6_ll[16];
} net_ifcfg_t;

int net_ifcfg_get(int index, net_ifcfg_t *out);
int net_ifcfg_set(int index, uint32_t ip, uint32_t netmask, uint32_t gateway, uint32_t dns);

#endif
