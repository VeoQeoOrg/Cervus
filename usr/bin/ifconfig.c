#include <stdio.h>
#include <sys/netcfg.h>

static void ip4(const char *label, uint32_t v) {
    printf("%s%u.%u.%u.%u", label,
           (v >> 24) & 0xff, (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
}

int main(void) {
    net_ifcfg_t c;
    int found = 0;
    for (int i = 0; i < 8; i++) {
        if (netif_get(i, &c) != 0) break;
        found = 1;
        printf("%s\tlink %s  mtu %d\n", c.name, c.link_up ? "UP" : "DOWN", c.mtu);
        printf("\tether %02x:%02x:%02x:%02x:%02x:%02x\n",
               c.mac[0], c.mac[1], c.mac[2], c.mac[3], c.mac[4], c.mac[5]);
        ip4("\tinet ", c.ip);
        ip4("  netmask ", c.netmask);
        putchar('\n');
        ip4("\tgateway ", c.gateway);
        ip4("  dns ", c.dns);
        putchar('\n');
        printf("\tRX packets %u bytes %u   TX packets %u bytes %u\n",
               (unsigned)c.rx_packets, (unsigned)c.rx_bytes,
               (unsigned)c.tx_packets, (unsigned)c.tx_bytes);
    }
    if (!found) printf("ifconfig: no network interfaces\n");
    return 0;
}
