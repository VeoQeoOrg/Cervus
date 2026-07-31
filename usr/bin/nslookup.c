#include <stdio.h>
#include <arpa/inet.h>
#include <sys/netcfg.h>

int main(int argc, char **argv) {
    if (argc < 2) { printf("usage: nslookup <name>\n"); return 1; }

    net_ifcfg_t c;
    if (netif_get(0, &c) == 0 && c.dns)
        printf("Server: %u.%u.%u.%u\n\n",
               (c.dns >> 24) & 0xff, (c.dns >> 16) & 0xff, (c.dns >> 8) & 0xff, c.dns & 0xff);

    in_addr_t a = inet_resolve(argv[1]);
    if (a == 0xffffffffu) { printf("** can't resolve '%s'\n", argv[1]); return 1; }

    struct in_addr ia; ia.s_addr = a;
    printf("Name:\t%s\nAddress: %s\n", argv[1], inet_ntoa(ia));
    return 0;
}
