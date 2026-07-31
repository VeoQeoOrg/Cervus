#ifndef _KERNEL_NET_DHCP_H
#define _KERNEL_NET_DHCP_H

#include "netdev.h"

void dhcp_start(netdev_t *dev);
int  dhcp_bound(void);

#endif
