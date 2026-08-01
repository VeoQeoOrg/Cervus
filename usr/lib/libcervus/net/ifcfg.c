#include <sys/netcfg.h>
#include <sys/syscall.h>
#include <libcervus.h>

int netif_get(int index, net_ifcfg_t *out) {
    return (int)__cervus_sys_ret(syscall2(SYS_NET_IFCFG, index, out));
}

int netif_set(int index, uint32_t ip, uint32_t netmask, uint32_t gateway, uint32_t dns) {
    return (int)__cervus_sys_ret(syscall5(SYS_NET_IFSET, index, ip, netmask, gateway, dns));
}
