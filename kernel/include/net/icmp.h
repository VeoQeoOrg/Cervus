#ifndef _KERNEL_NET_ICMP_H
#define _KERNEL_NET_ICMP_H

#include <stdint.h>
#include <stddef.h>
#include "netdev.h"

void icmp_rx(netdev_t *dev, uint32_t src, const uint8_t *pkt, size_t len);
int  icmp_send_echo(netdev_t *dev, uint32_t dst, uint16_t id, uint16_t seq);

#endif
