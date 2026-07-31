#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/netcfg.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        int fd = open("/etc/resolv.conf", O_RDONLY);
        if (fd >= 0) {
            char buf[256];
            int n = (int)read(fd, buf, sizeof(buf) - 1);
            close(fd);
            if (n > 0) { buf[n] = 0; printf("%s", buf); }
        } else {
            printf("no /etc/resolv.conf\n");
        }
        net_ifcfg_t c;
        if (netif_get(0, &c) == 0 && c.dns)
            printf("(DHCP DNS: %u.%u.%u.%u)\n",
                   (c.dns >> 24) & 0xff, (c.dns >> 16) & 0xff, (c.dns >> 8) & 0xff, c.dns & 0xff);
        return 0;
    }

    if (inet_addr(argv[1]) == 0xffffffffu) {
        printf("setdns: invalid IP address '%s'\n", argv[1]);
        return 1;
    }

    int fd = open("/etc/resolv.conf", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { printf("setdns: cannot write /etc/resolv.conf\n"); return 1; }
    char line[128];
    int n = snprintf(line, sizeof(line), "nameserver %s\n", argv[1]);
    write(fd, line, (size_t)n);
    close(fd);
    printf("DNS server set to %s\n", argv[1]);
    return 0;
}
