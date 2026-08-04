#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 9996

int main(void) {
    int srv = socket(AF_INET6, SOCK_DGRAM, 0);
    if (srv < 0) { printf("socket(AF_INET6) FAILED\n"); return 1; }
    struct sockaddr_in6 sa; memset(&sa, 0, sizeof sa);
    sa.sin6_family = AF_INET6; sa.sin6_port = htons(PORT); sa.sin6_addr = in6addr_any;
    if (bind(srv, (struct sockaddr *)&sa, sizeof sa) < 0) { printf("bind FAILED\n"); return 1; }

    int cli = socket(AF_INET6, SOCK_DGRAM, 0);
    struct sockaddr_in6 da; memset(&da, 0, sizeof da);
    da.sin6_family = AF_INET6; da.sin6_port = htons(PORT);
    inet_pton(AF_INET6, "::1", &da.sin6_addr);

    const char *msg = "ipv6-udp-hello";
    printf("sending '%s' to [::1]:%d over IPv6 ...\n", msg, PORT);
    if (sendto(cli, msg, strlen(msg), 0, (struct sockaddr *)&da, sizeof da) < 0) { printf("sendto FAILED\n"); return 1; }

    char buf[128]; struct sockaddr_in6 from; socklen_t fl = sizeof from;
    long n = recvfrom(srv, buf, sizeof buf, 0, (struct sockaddr *)&from, &fl);
    buf[n > 0 ? n : 0] = 0;
    char fs[64]; inet_ntop(AF_INET6, &from.sin6_addr, fs, sizeof fs);
    printf("received %ld bytes '%s' from [%s]:%d\n", n, buf, fs, ntohs(from.sin6_port));

    int ok = (n == (long)strlen(msg)) && !strcmp(buf, msg);
    close(srv); close(cli);
    printf("\n%s\n", ok ? "IPv6 UDP WORKS (::1)" : "IPv6 UDP FAILED");
    return ok ? 0 : 1;
}
