#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/netcfg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libcervus.h>

#define IF_SCAN_MAX 16

static unsigned flags_of(const net_ifcfg_t *c)
{
    unsigned f = IFF_UP;
    if (c->link_up) f |= IFF_RUNNING;
    if (c->is_loopback) f |= IFF_LOOPBACK;
    else f |= IFF_BROADCAST | IFF_MULTICAST;
    return f;
}

unsigned int if_nametoindex(const char *name)
{
    net_ifcfg_t c;
    for (int i = 0; i < IF_SCAN_MAX; i++) {
        if (netif_get(i, &c) != 0) break;
        if (!strncmp(c.name, name, sizeof c.name)) return (unsigned)i + 1;
    }
    __cervus_set_errno(ENXIO);
    return 0;
}

char *if_indextoname(unsigned int index, char *name)
{
    net_ifcfg_t c;
    if (index == 0 || netif_get((int)index - 1, &c) != 0) { __cervus_set_errno(ENXIO); return NULL; }
    strncpy(name, c.name, IF_NAMESIZE - 1);
    name[IF_NAMESIZE - 1] = 0;
    return name;
}

struct if_nameindex *if_nameindex(void)
{
    net_ifcfg_t c;
    int n = 0;
    while (n < IF_SCAN_MAX && netif_get(n, &c) == 0) n++;
    struct if_nameindex *r = calloc((size_t)n + 1, sizeof *r + IF_NAMESIZE);
    if (!r) return NULL;
    char *names = (char *)(r + n + 1);
    for (int i = 0; i < n; i++) {
        netif_get(i, &c);
        r[i].if_index = (unsigned)i + 1;
        r[i].if_name = names + i * IF_NAMESIZE;
        strncpy(r[i].if_name, c.name, IF_NAMESIZE - 1);
    }
    return r;
}

void if_freenameindex(struct if_nameindex *ptr)
{
    free(ptr);
}

static struct sockaddr *v4(uint32_t ip)
{
    struct sockaddr_in *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->sin_family = AF_INET;
    s->sin_addr.s_addr = htonl(ip);
    return (struct sockaddr *)s;
}

int getifaddrs(struct ifaddrs **ifap)
{
    struct ifaddrs *head = NULL, **tail = &head;
    net_ifcfg_t c;
    for (int i = 0; i < IF_SCAN_MAX && netif_get(i, &c) == 0; i++) {
        struct ifaddrs *a = calloc(1, sizeof *a + IF_NAMESIZE);
        if (!a) { freeifaddrs(head); __cervus_set_errno(ENOMEM); return -1; }
        a->ifa_name = (char *)(a + 1);
        strncpy(a->ifa_name, c.name, IF_NAMESIZE - 1);
        a->ifa_flags = flags_of(&c);
        a->ifa_addr = v4(c.ip);
        a->ifa_netmask = v4(c.netmask);
        if (!c.is_loopback && c.netmask)
            a->ifa_broadaddr = v4((c.ip & c.netmask) | ~c.netmask);
        *tail = a;
        tail = &a->ifa_next;
    }
    *ifap = head;
    return 0;
}

void freeifaddrs(struct ifaddrs *ifa)
{
    while (ifa) {
        struct ifaddrs *n = ifa->ifa_next;
        free(ifa->ifa_addr);
        free(ifa->ifa_netmask);
        free(ifa->ifa_broadaddr);
        free(ifa);
        ifa = n;
    }
}
