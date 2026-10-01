#include <http.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/netcfg.h>
#include <netinet/in.h>
#include <arpa/inet.h>

extern in_addr_t inet_resolve(const char *name);

static int url_host_port(const char *url, char *host, size_t cap, int *port) {
    int https = 0;
    if (!strncmp(url, "https://", 8)) { https = 1; url += 8; }
    else if (!strncmp(url, "http://", 7)) url += 7;
    const char *at = strchr(url, '@');
    const char *slash = strchr(url, '/');
    if (at && (!slash || at < slash)) url = at + 1;
    size_t n = 0;
    while (url[n] && url[n] != '/' && url[n] != ':' && url[n] != '?') n++;
    if (n == 0 || n >= cap) return -1;
    memcpy(host, url, n);
    host[n] = 0;
    *port = https ? 443 : 80;
    if (url[n] == ':') *port = atoi(url + n + 1);
    return 0;
}

static void say(char *why, size_t cap, const char *msg) {
    if (why && cap) snprintf(why, cap, "%s", msg);
}

int net_check(const char *url, int timeout_ms, char *why, size_t whycap) {
    int any_link = 0, any_addr = 0;
    for (int i = 0; i < 16; i++) {
        net_ifcfg_t c;
        if (netif_get(i, &c) != 0) break;
        if (c.is_loopback) continue;
        if (c.link_up) any_link = 1;
        if (c.link_up && c.ip) any_addr = 1;
    }
    if (!any_link) {
        say(why, whycap, "no network: no cable is plugged in and no Wi-Fi network is joined");
        return NET_NO_LINK;
    }
    if (!any_addr) {
        say(why, whycap, "connected, but no address was handed out (DHCP has not answered yet)");
        return NET_NO_ADDRESS;
    }

    char host[256];
    int port = 0;
    if (!url || url_host_port(url, host, sizeof host, &port) != 0) {
        say(why, whycap, "the repository address is not a valid URL");
        return NET_BAD_URL;
    }

    in_addr_t ip = inet_resolve(host);
    if (ip == 0xffffffffu) {
        char m[320];
        snprintf(m, sizeof m, "the name %s cannot be resolved: the DNS server does not answer", host);
        say(why, whycap, m);
        return NET_NO_DNS;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { say(why, whycap, "cannot open a socket"); return NET_UNREACHABLE; }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    sa.sin_addr.s_addr = ip;
    int r = connect(fd, (struct sockaddr *)&sa, sizeof sa);
    if (r < 0 && errno == EINPROGRESS) {
        struct pollfd p = { fd, POLLOUT, 0 };
        r = poll(&p, 1, timeout_ms > 0 ? timeout_ms : 5000);
        if (r > 0) {
            int err = 0;
            socklen_t el = sizeof err;
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el);
            r = err ? -1 : 0;
        } else {
            r = -1;
        }
    }
    close(fd);
    if (r != 0) {
        char m[320];
        snprintf(m, sizeof m, "%s does not answer on port %d", host, port);
        say(why, whycap, m);
        return NET_UNREACHABLE;
    }
    say(why, whycap, "online");
    return NET_ONLINE;
}
