#ifndef _NET_ROUTE_H
#define _NET_ROUTE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>

struct rtentry {
    unsigned long   rt_pad1;
    struct sockaddr rt_dst;
    struct sockaddr rt_gateway;
    struct sockaddr rt_genmask;
    unsigned short  rt_flags;
    short           rt_pad2;
    unsigned long   rt_pad3;
    unsigned char   rt_tos;
    unsigned char   rt_class;
    short           rt_pad4[3];
    short           rt_metric;
    char           *rt_dev;
    unsigned long   rt_mtu;
    unsigned long   rt_window;
    unsigned short  rt_irtt;
};

#define rt_mss rt_mtu

#define RTF_UP        0x0001
#define RTF_GATEWAY   0x0002
#define RTF_HOST      0x0004
#define RTF_REINSTATE 0x0008
#define RTF_DYNAMIC   0x0010
#define RTF_MODIFIED  0x0020
#define RTF_MTU       0x0040
#define RTF_MSS       RTF_MTU
#define RTF_WINDOW    0x0080
#define RTF_IRTT      0x0100
#define RTF_REJECT    0x0200
#define RTF_STATIC    0x0400
#define RTF_XRESOLVE  0x0800
#define RTF_NOFORWARD 0x1000
#define RTF_THROW     0x2000
#define RTF_NOPMTUDISC 0x4000
#define RTF_DEFAULT   0x00010000
#define RTF_ALLONLINK 0x00020000
#define RTF_ADDRCONF  0x00040000
#define RTF_LINKRT    0x00100000
#define RTF_NONEXTHOP 0x00200000
#define RTF_CACHE     0x01000000
#define RTF_FLOW      0x02000000
#define RTF_POLICY    0x04000000
#define RTF_LOCAL     0x80000000
#define RTF_INTERFACE 0x40000000
#define RTF_MULTICAST 0x20000000
#define RTF_BROADCAST 0x10000000
#define RTF_NAT       0x08000000

#ifdef __cplusplus
}
#endif
#endif
