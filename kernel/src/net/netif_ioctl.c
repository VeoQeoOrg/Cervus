#include "../../include/net/netdev.h"
#include "../../include/net/net.h"
#include "../../include/net/socket.h"
#include "../../include/syscall/syscall_internal.h"
#include "../../include/sched/capabilities.h"
#include <string.h>

#define SIOCGIFNAME    0x8910
#define SIOCGIFFLAGS   0x8913
#define SIOCSIFFLAGS   0x8914
#define SIOCGIFADDR    0x8915
#define SIOCSIFADDR    0x8916
#define SIOCGIFDSTADDR 0x8917
#define SIOCGIFBRDADDR 0x8919
#define SIOCGIFNETMASK 0x891b
#define SIOCSIFNETMASK 0x891c
#define SIOCGIFMETRIC  0x891d
#define SIOCGIFMTU     0x8921
#define SIOCSIFMTU     0x8922
#define SIOCGIFHWADDR  0x8927
#define SIOCGIFINDEX   0x8933
#define SIOCGIFTXQLEN  0x8942
#define SIOCGIWNAME    0x8b01

#define IFF_UP_K          0x1
#define IFF_BROADCAST_K   0x2
#define IFF_LOOPBACK_K    0x8
#define IFF_RUNNING_K     0x40
#define IFF_MULTICAST_K   0x1000
#define IFF_LOWER_UP_K    0x10000

#define ARPHRD_ETHER_K    1
#define ARPHRD_LOOPBACK_K 772

typedef struct {
    char name[16];
    union {
        struct {
            uint16_t family;
            uint8_t  data[14];
        } addr;
        int16_t  flags;
        int32_t  ivalue;
        uint8_t  raw[24];
    } u;
} k_ifreq_t;

typedef struct {
    int32_t  len;
    uint32_t _pad;
    uint64_t buf;
} k_ifconf_t;

static void put_in4(k_ifreq_t *r, uint32_t ip)
{
    memset(&r->u, 0, sizeof r->u);
    r->u.addr.family = AF_INET;
    r->u.addr.data[2] = (uint8_t)(ip >> 24);
    r->u.addr.data[3] = (uint8_t)(ip >> 16);
    r->u.addr.data[4] = (uint8_t)(ip >> 8);
    r->u.addr.data[5] = (uint8_t)ip;
}

static uint32_t get_in4(const k_ifreq_t *r)
{
    const uint8_t *d = r->u.addr.data;
    return ((uint32_t)d[2] << 24) | ((uint32_t)d[3] << 16) | ((uint32_t)d[4] << 8) | d[5];
}

static int is_wireless(const netdev_t *d)
{
    return !strncmp(d->name, "wlan", 4);
}

static int dev_flags(const netdev_t *d)
{
    int f = IFF_UP_K;
    if (d->link_up) f |= IFF_RUNNING_K | IFF_LOWER_UP_K;
    if (d->is_loopback) f |= IFF_LOOPBACK_K | IFF_RUNNING_K | IFF_LOWER_UP_K;
    else f |= IFF_BROADCAST_K | IFF_MULTICAST_K;
    return f;
}

static int may_configure(void)
{
    task_t *t = syscall_cur_task();
    return t && (t->uid == 0 || cap_has(t->capabilities, CAP_SYSADMIN));
}

int64_t netdev_ifreq_ioctl(uint64_t req, void *arg)
{
    if (!arg) return -EFAULT;
    k_ifreq_t *r = arg;

    if (req == SIOCGIFNAME) {
        netdev_t *d = r->u.ivalue > 0 ? netdev_at(r->u.ivalue - 1) : NULL;
        if (!d) return -ENODEV;
        memset(r->name, 0, sizeof r->name);
        strncpy(r->name, d->name, sizeof r->name - 1);
        return 0;
    }

    char name[17];
    memcpy(name, r->name, 16);
    name[16] = 0;
    netdev_t *d = netdev_get(name);
    if (!d) return -ENODEV;

    switch (req) {
    case SIOCGIFFLAGS:
        r->u.ivalue = 0;
        r->u.flags = (int16_t)dev_flags(d);
        return 0;
    case SIOCSIFFLAGS:
        return may_configure() ? 0 : -EPERM;
    case SIOCGIFADDR:
        if (!d->ip) return -EADDRNOTAVAIL;
        put_in4(r, d->ip);
        return 0;
    case SIOCGIFDSTADDR:
        put_in4(r, d->is_loopback ? d->ip : 0);
        return 0;
    case SIOCGIFBRDADDR:
        if (!d->ip || d->is_loopback) return -EADDRNOTAVAIL;
        put_in4(r, (d->ip & d->netmask) | ~d->netmask);
        return 0;
    case SIOCGIFNETMASK:
        if (!d->ip) return -EADDRNOTAVAIL;
        put_in4(r, d->netmask);
        return 0;
    case SIOCSIFADDR:
        if (!may_configure()) return -EPERM;
        if (d->is_loopback || r->u.addr.family != AF_INET) return -EINVAL;
        d->ip = get_in4(r);
        d->static_cfg = 1;
        return 0;
    case SIOCSIFNETMASK:
        if (!may_configure()) return -EPERM;
        if (d->is_loopback || r->u.addr.family != AF_INET) return -EINVAL;
        d->netmask = get_in4(r);
        return 0;
    case SIOCGIFMETRIC:
        r->u.ivalue = d->metric;
        return 0;
    case SIOCGIFMTU:
        r->u.ivalue = (int32_t)d->mtu;
        return 0;
    case SIOCSIFMTU:
        if (!may_configure()) return -EPERM;
        if (r->u.ivalue < 68 || r->u.ivalue > 65535) return -EINVAL;
        d->mtu = (uint32_t)r->u.ivalue;
        return 0;
    case SIOCGIFHWADDR:
        memset(&r->u, 0, sizeof r->u);
        r->u.addr.family = d->is_loopback ? ARPHRD_LOOPBACK_K : ARPHRD_ETHER_K;
        memcpy(r->u.addr.data, d->mac, ETH_ALEN);
        return 0;
    case SIOCGIFINDEX:
        r->u.ivalue = netdev_index_of(d);
        return 0;
    case SIOCGIFTXQLEN:
        r->u.ivalue = d->is_loopback ? 1000 : 1000;
        return 0;
    case SIOCGIWNAME:
        if (!is_wireless(d)) return -EOPNOTSUPP;
        memset(&r->u, 0, sizeof r->u);
        memcpy(r->u.raw, "IEEE 802.11", 11);
        return 0;
    default:
        return -ENOTTY;
    }
}

int64_t netdev_ifconf(void *arg)
{
    if (!arg) return -EFAULT;
    k_ifconf_t *c = arg;
    int32_t used = 0;
    for (int i = 0; ; i++) {
        netdev_t *d = netdev_at(i);
        if (!d) break;
        if (!d->ip) continue;
        if (c->buf) {
            if (used + (int32_t)sizeof(k_ifreq_t) > c->len) break;
            k_ifreq_t r;
            memset(&r, 0, sizeof r);
            strncpy(r.name, d->name, sizeof r.name - 1);
            put_in4(&r, d->ip);
            if (syscall_copy_to_user((void *)(c->buf + (uint64_t)used), &r, sizeof r) < 0)
                return -EFAULT;
        }
        used += (int32_t)sizeof(k_ifreq_t);
    }
    c->len = used;
    return 0;
}
