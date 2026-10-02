#ifndef _KERNEL_NET_DHCP_H
#define _KERNEL_NET_DHCP_H

#include "netdev.h"

void dhcp_tick(uint64_t now_ns);
int  dhcp_bound(netdev_t *dev);

#endif
