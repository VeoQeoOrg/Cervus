#ifndef _KERNEL_NET_DNS_H
#define _KERNEL_NET_DNS_H

#include "netdev.h"

void dns_query(netdev_t *dev, const char *name);

#endif
