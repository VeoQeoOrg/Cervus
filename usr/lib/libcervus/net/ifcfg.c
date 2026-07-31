#include <sys/netcfg.h>
#include <sys/syscall.h>
#include <libcervus.h>

int netif_get(int index, net_ifcfg_t *out) {
    return (int)__cervus_sys_ret(syscall2(SYS_NET_IFCFG, index, out));
}
