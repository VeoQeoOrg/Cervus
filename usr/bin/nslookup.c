#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/netcfg.h>

int main(int argc, char **argv) {
    if (argc < 2) { printf("usage: nslookup <name>\n"); return 1; }

    int shown = 0;
    int rf = open("/etc/resolv.conf", O_RDONLY);
    if (rf >= 0) {
        char b[256];
        int n = (int)read(rf, b, sizeof(b) - 1);
        close(rf);
        if (n > 0) {
            b[n] = 0;
            char *p = strstr(b, "nameserver");
            if (p) {
                p += 10;
                while (*p == ' ' || *p == '\t') p++;
                char ip[64]; int i = 0;
                while (*p && *p != '\n' && *p != ' ' && i < 63) ip[i++] = *p++;
                ip[i] = 0;
                printf("Server: %s\n\n", ip);
                shown = 1;
            }
        }
    }
    net_ifcfg_t c;
    if (!shown && netif_get(0, &c) == 0 && c.dns)
        printf("Server: %u.%u.%u.%u\n\n",
               (c.dns >> 24) & 0xff, (c.dns >> 16) & 0xff, (c.dns >> 8) & 0xff, c.dns & 0xff);

    in_addr_t a = inet_resolve(argv[1]);
    if (a == 0xffffffffu) { printf("** can't resolve '%s'\n", argv[1]); return 1; }

    struct in_addr ia; ia.s_addr = a;
    printf("Name:\t%s\nAddress: %s\n", argv[1], inet_ntoa(ia));
    return 0;
}
