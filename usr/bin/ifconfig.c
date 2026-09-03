#include <stdio.h>
#include <string.h>
#include <sys/netcfg.h>

static void ip4(const char *label, uint32_t v) {
    printf("%s%u.%u.%u.%u", label,
           (v >> 24) & 0xff, (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
}

static uint32_t parse_ip(const char *s) {
    uint32_t v = 0;
    int part = 0, seen = 0, octets = 0;
    for (const char *p = s; ; p++) {
        if (*p >= '0' && *p <= '9') { part = part * 10 + (*p - '0'); seen = 1; }
        else if (*p == '.' || *p == 0) {
            if (!seen || part > 255) return 0;
            v = (v << 8) | (uint32_t)part;
            octets++; part = 0; seen = 0;
            if (*p == 0) break;
        } else return 0;
    }
    return octets == 4 ? v : 0;
}

static int name_to_index(const char *name) {
    net_ifcfg_t c;
    for (int i = 0; i < 8; i++) {
        if (netif_get(i, &c) != 0) break;
        if (strcmp(c.name, name) == 0) return i;
    }
    return -1;
}

static void show(void) {
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
        if (c.rx_dropped || c.tx_dropped)
            printf("\tRX dropped %u   TX dropped %u\n",
                   (unsigned)c.rx_dropped, (unsigned)c.tx_dropped);
    }
    if (!found) printf("ifconfig: no network interfaces\n");
}

int main(int argc, char **argv) {
    if (argc < 3) { show(); return 0; }

    int idx = name_to_index(argv[1]);
    if (idx < 0) { printf("ifconfig: no such interface %s\n", argv[1]); return 1; }
    uint32_t ip = parse_ip(argv[2]);
    if (!ip) { printf("ifconfig: bad address %s\n", argv[2]); return 1; }
    uint32_t netmask = 0xffffff00u, gw = 0, dns = 0;
    for (int i = 3; i + 1 < argc; i += 2) {
        uint32_t v = parse_ip(argv[i + 1]);
        if (strcmp(argv[i], "netmask") == 0) netmask = v;
        else if (strcmp(argv[i], "gw") == 0 || strcmp(argv[i], "gateway") == 0) gw = v;
        else if (strcmp(argv[i], "dns") == 0) dns = v;
    }
    if (netif_set(idx, ip, netmask, gw, dns) != 0) { printf("ifconfig: set failed\n"); return 1; }
    ip4("configured ", ip); ip4("/", netmask); putchar('\n');
    return 0;
}
