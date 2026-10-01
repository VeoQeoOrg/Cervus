#ifndef _NETINET_IN_H
#define _NETINET_IN_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <sys/socket.h>

#define INET_ADDRSTRLEN  16
#define INET6_ADDRSTRLEN 46

#define INADDR_ANY       0u
#define INADDR_LOOPBACK  0x7f000001u
#define INADDR_BROADCAST 0xffffffffu
#define INADDR_NONE      0xffffffffu

#define IPPROTO_IP     0
#define IPPROTO_ICMP   1
#define IPPROTO_IPV6   41
#define IPPROTO_RAW    255
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

#define IP_TOS                    1
#define IP_TTL                    2
#define IP_HDRINCL                3
#define IP_RECVERR                11
#define IP_MULTICAST_IF           32
#define IP_MULTICAST_TTL          33
#define IP_MULTICAST_LOOP         34
#define IP_ADD_MEMBERSHIP         35
#define IP_DROP_MEMBERSHIP        36
#define IP_ADD_SOURCE_MEMBERSHIP  39
#define IP_DROP_SOURCE_MEMBERSHIP 40
#define IPV6_UNICAST_HOPS         16
#define IPV6_MULTICAST_IF         17
#define IPV6_MULTICAST_HOPS       18
#define IPV6_MULTICAST_LOOP       19
#define IPV6_ADD_MEMBERSHIP       20
#define IPV6_DROP_MEMBERSHIP      21
#define IPV6_JOIN_GROUP           IPV6_ADD_MEMBERSHIP
#define IPV6_LEAVE_GROUP          IPV6_DROP_MEMBERSHIP
#define IPV6_RECVERR              25
#define IPV6_V6ONLY               26
#define MCAST_JOIN_SOURCE_GROUP   46
#define MCAST_LEAVE_SOURCE_GROUP  47

struct ip_mreq {
    struct in_addr imr_multiaddr;
    struct in_addr imr_interface;
};

struct ip_mreq_source {
    struct in_addr imr_multiaddr;
    struct in_addr imr_interface;
    struct in_addr imr_sourceaddr;
};

struct ipv6_mreq {
    struct in6_addr ipv6mr_multiaddr;
    unsigned int    ipv6mr_interface;
};

struct sockaddr_storage;
struct group_source_req {
    uint32_t gsr_interface;
    uint32_t __pad;
    struct {
        uint16_t ss_family;
        char     __data[126];
    } gsr_group, gsr_source;
};

#ifdef __cplusplus
}
#endif
#endif
