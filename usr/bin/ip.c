#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/netcfg.h>

static uint32_t parse_ip(const char *s) {
    uint32_t v = 0; int part = 0, seen = 0, octets = 0;
    for (const char *p = s; ; p++) {
        if (*p >= '0' && *p <= '9') { part = part * 10 + (*p - '0'); seen = 1; }
        else if (*p == '.' || *p == 0 || *p == '/') {
            if (!seen || part > 255) return 0;
            v = (v << 8) | (uint32_t)part; octets++; part = 0; seen = 0;
            if (*p == 0 || *p == '/') break;
        } else return 0;
    }
    return octets == 4 ? v : 0;
}

static uint32_t prefix_to_mask(int p) { return p <= 0 ? 0 : (p >= 32 ? 0xffffffffu : (0xffffffffu << (32 - p))); }
static int mask_to_prefix(uint32_t m) { int n = 0; while (m & 0x80000000u) { n++; m <<= 1; } return n; }

static void pip(uint32_t v) { printf("%u.%u.%u.%u", (v >> 24) & 255, (v >> 16) & 255, (v >> 8) & 255, v & 255); }

static int find_dev(char **argv, int argc) {
    for (int i = 0; i < argc; i++) if (!strcmp(argv[i], "dev") && i + 1 < argc) {
        net_ifcfg_t c;
        for (int k = 0; k < 8; k++) { if (netif_get(k, &c) != 0) break; if (!strcmp(c.name, argv[i + 1])) return k; }
        return -1;
    }
    return 0;
}

static void show_addr(void) {
    net_ifcfg_t c;
    for (int i = 0; i < 8; i++) {
        if (netif_get(i, &c) != 0) break;
        printf("%d: %s: <BROADCAST,MULTICAST%s> mtu %d state %s\n",
               i + 1, c.name, c.link_up ? ",UP,LOWER_UP" : "", c.mtu, c.link_up ? "UP" : "DOWN");
        printf("    link/ether %02x:%02x:%02x:%02x:%02x:%02x brd ff:ff:ff:ff:ff:ff\n",
               c.mac[0], c.mac[1], c.mac[2], c.mac[3], c.mac[4], c.mac[5]);
        if (c.ip) {
            uint32_t brd = c.ip | ~c.netmask;
            printf("    inet "); pip(c.ip); printf("/%d brd ", mask_to_prefix(c.netmask)); pip(brd);
            printf(" scope global %s\n", c.name);
        }
    }
}

static void show_link(void) {
    net_ifcfg_t c;
    for (int i = 0; i < 8; i++) {
        if (netif_get(i, &c) != 0) break;
        printf("%d: %s: <BROADCAST,MULTICAST%s> mtu %d state %s\n",
               i + 1, c.name, c.link_up ? ",UP,LOWER_UP" : "", c.mtu, c.link_up ? "UP" : "DOWN");
        printf("    link/ether %02x:%02x:%02x:%02x:%02x:%02x brd ff:ff:ff:ff:ff:ff\n",
               c.mac[0], c.mac[1], c.mac[2], c.mac[3], c.mac[4], c.mac[5]);
    }
}

static void show_route(void) {
    net_ifcfg_t c;
    for (int i = 0; i < 8; i++) {
        if (netif_get(i, &c) != 0) break;
        if (c.gateway) { printf("default via "); pip(c.gateway); printf(" dev %s\n", c.name); }
    }
    for (int i = 0; i < 8; i++) {
        if (netif_get(i, &c) != 0) break;
        if (c.ip && c.netmask) { pip(c.ip & c.netmask); printf("/%d dev %s proto kernel scope link src ", mask_to_prefix(c.netmask), c.name); pip(c.ip); putchar('\n'); }
    }
}

static int addr_change(char **argv, int argc, int del) {
    const char *spec = 0;
    for (int i = 0; i < argc; i++) if (strchr(argv[i], '.')) { spec = argv[i]; break; }
    if (!spec) { fprintf(stderr, "ip: expected address\n"); return 1; }
    uint32_t ip = parse_ip(spec);
    const char *sl = strchr(spec, '/');
    uint32_t mask = sl ? prefix_to_mask(atoi(sl + 1)) : 0xffffff00u;
    int idx = find_dev(argv, argc);
    if (idx < 0) { fprintf(stderr, "ip: no such device\n"); return 1; }
    if (del) { ip = 0; mask = 0; }
    if (netif_set(idx, ip, mask, 0, 0) != 0) { fprintf(stderr, "ip: set failed\n"); return 1; }
    return 0;
}

static int route_change(char **argv, int argc, int del) {
    uint32_t gw = 0;
    for (int i = 0; i < argc; i++) if (!strcmp(argv[i], "via") && i + 1 < argc) gw = parse_ip(argv[i + 1]);
    int idx = find_dev(argv, argc);
    if (idx < 0) idx = 0;
    net_ifcfg_t c;
    if (netif_get(idx, &c) != 0) { fprintf(stderr, "ip: no device\n"); return 1; }
    if (!del && !gw) { fprintf(stderr, "ip: route add needs 'via <gw>'\n"); return 1; }
    uint32_t setgw = del ? 0xffffffffu : gw;
    if (netif_set(idx, c.ip, c.netmask, setgw, 0) != 0) { fprintf(stderr, "ip: set failed\n"); return 1; }
    return 0;
}

static void usage(void) {
    printf("Usage: ip [ OBJECT ] [ COMMAND ]\n"
           "  ip addr [show]                     show addresses\n"
           "  ip addr add <ip>/<prefix> dev <if> assign address\n"
           "  ip addr del <ip>/<prefix> dev <if> remove address\n"
           "  ip link [show]                     show interfaces\n"
           "  ip route [show]                    show routes\n"
           "  ip route add default via <gw>      set default gateway\n"
           "  ip route del default               clear default gateway\n");
}

static int is(const char *a, const char *b) { return !strcmp(a, b); }

int main(int argc, char **argv) {
    if (argc < 2) { show_addr(); return 0; }
    const char *o = argv[1];
    char **rest = argv + 3; int nrest = argc - 3;

    if (is(o, "addr") || is(o, "a") || is(o, "address")) {
        if (argc < 3 || is(argv[2], "show") || is(argv[2], "list") || is(argv[2], "ls")) { show_addr(); return 0; }
        if (is(argv[2], "add")) return addr_change(rest, nrest, 0);
        if (is(argv[2], "del") || is(argv[2], "delete")) return addr_change(rest, nrest, 1);
    } else if (is(o, "link") || is(o, "l")) {
        if (argc < 3 || is(argv[2], "show")) { show_link(); return 0; }
        if (is(argv[2], "set")) { printf("ip: link up/down is driver-managed (NIC always up)\n"); return 0; }
    } else if (is(o, "route") || is(o, "r")) {
        if (argc < 3 || is(argv[2], "show") || is(argv[2], "list")) { show_route(); return 0; }
        if (is(argv[2], "add")) return route_change(rest, nrest, 0);
        if (is(argv[2], "del") || is(argv[2], "delete")) return route_change(rest, nrest, 1);
    } else if (is(o, "-h") || is(o, "help") || is(o, "--help")) { usage(); return 0; }

    usage();
    return 1;
}
