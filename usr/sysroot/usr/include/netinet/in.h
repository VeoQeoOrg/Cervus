#ifndef _NETINET_IN_H
#define _NETINET_IN_H

#include <stdint.h>
#include <sys/socket.h>

#define INADDR_ANY       0u
#define INADDR_BROADCAST 0xffffffffu

#define IPPROTO_ICMP   1
#define IPPROTO_TCP    6
#define IPPROTO_UDP    17
#define IPPROTO_ICMPV6 58

typedef uint32_t in_addr_t;
typedef uint16_t in_port_t;

struct in_addr { in_addr_t s_addr; };

struct sockaddr_in {
    uint16_t       sin_family;
    uint16_t       sin_port;
    struct in_addr sin_addr;
    uint8_t        sin_zero[8];
};

struct in6_addr { uint8_t s6_addr[16]; };

struct sockaddr_in6 {
    uint16_t        sin6_family;
    uint16_t        sin6_port;
    uint32_t        sin6_flowinfo;
    struct in6_addr sin6_addr;
    uint32_t        sin6_scope_id;
};

#define IN6ADDR_ANY_INIT      {{ 0 }}
#define IN6ADDR_LOOPBACK_INIT {{ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,1 }}
extern const struct in6_addr in6addr_any;
extern const struct in6_addr in6addr_loopback;

static inline uint16_t htons(uint16_t x) { return (uint16_t)((x << 8) | (x >> 8)); }
static inline uint16_t ntohs(uint16_t x) { return htons(x); }
static inline uint32_t htonl(uint32_t x) {
    return ((x & 0xffu) << 24) | ((x & 0xff00u) << 8) |
           ((x >> 8) & 0xff00u) | ((x >> 24) & 0xffu);
}
static inline uint32_t ntohl(uint32_t x) { return htonl(x); }

#endif
