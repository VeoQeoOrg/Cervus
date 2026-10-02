#include <sys/netcfg.h>
#include <sys/syscall.h>
#include <libcervus.h>

int netif_get(int index, net_ifcfg_t *out) {
    return (int)__cervus_sys_ret(syscall3(SYS_NET_IFCFG, index, out, sizeof(*out)));
}

int netif_set(int index, uint32_t ip, uint32_t netmask, uint32_t gateway, uint32_t dns) {
    return (int)__cervus_sys_ret(syscall5(SYS_NET_IFSET, index, ip, netmask, gateway, dns));
}

uint32_t netif_default_dns(void) {
    net_ifcfg_t c;
    uint32_t up = 0, any = 0;
    for (int i = 0; i < 16 && netif_get(i, &c) == 0; i++) {
        if (c.is_loopback || !c.dns) continue;
        if (c.is_default) return c.dns;
        if (c.link_up && !up) up = c.dns;
        if (!any) any = c.dns;
    }
    return up ? up : any;
}
