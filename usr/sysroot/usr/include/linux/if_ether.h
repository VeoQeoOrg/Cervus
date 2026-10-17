#ifndef _LINUX_IF_ETHER_H
#define _LINUX_IF_ETHER_H

#include <stdint.h>

#define ETH_ALEN       6
#define ETH_TLEN       2
#define ETH_HLEN       14
#define ETH_ZLEN       60
#define ETH_DATA_LEN   1500
#define ETH_FRAME_LEN  1514
#define ETH_FCS_LEN    4
#define ETH_MIN_MTU    68
#define ETH_MAX_MTU    0xFFFFU

#define ETH_P_LOOP     0x0060
#define ETH_P_IP       0x0800
#define ETH_P_ARP      0x0806
#define ETH_P_RARP     0x8035
#define ETH_P_8021Q    0x8100
#define ETH_P_IPV6     0x86DD
#define ETH_P_PAE      0x888E
#define ETH_P_8021AD   0x88A8
#define ETH_P_ALL      0x0003

struct ethhdr {
    unsigned char h_dest[ETH_ALEN];
    unsigned char h_source[ETH_ALEN];
    uint16_t      h_proto;
} __attribute__((packed));

#endif
