#ifndef _KERNEL_NET_IPV6_H
#define _KERNEL_NET_IPV6_H

#include <stdint.h>
#include <stddef.h>
#include "netdev.h"

void ipv6_rx(netdev_t *dev, const uint8_t *smac, const uint8_t *pkt, size_t len);
int  ipv6_output(netdev_t *dev, const uint8_t *src, const uint8_t *dst,
                 uint8_t nexthdr, const uint8_t *payload, uint32_t plen);
void ipv6_resolve(netdev_t *dev, const uint8_t *target);
int  ipv6_loopback_drain_one(void);
void ipv6_get_lladdr(netdev_t *dev, uint8_t out[16]);

#endif
