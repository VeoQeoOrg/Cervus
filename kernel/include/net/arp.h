#ifndef _KERNEL_NET_ARP_H
#define _KERNEL_NET_ARP_H

#include <stdint.h>
#include <stddef.h>
#include "netdev.h"

void arp_rx(netdev_t *dev, const uint8_t *pkt, size_t len);
void arp_request(netdev_t *dev, uint32_t target_ip);
int  arp_lookup(uint32_t ip, uint8_t mac_out[6]);
void arp_age(void);

#endif
