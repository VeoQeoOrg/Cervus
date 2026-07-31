#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/cervus.h>

static uint16_t icmp_csum(const uint8_t *d, int len) {
    uint32_t sum = 0;
    for (int i = 0; i + 1 < len; i += 2) sum += (uint32_t)((d[i] << 8) | d[i + 1]);
    if (len & 1) sum += (uint32_t)(d[len - 1] << 8);
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}

int main(int argc, char **argv) {
    int count = 4;
    const char *host = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) count = atoi(argv[++i]);
        else host = argv[i];
    }
    if (!host) { printf("usage: ping [-c count] <host>\n"); return 1; }

    in_addr_t dst = inet_resolve(host);
    if (dst == 0xffffffffu) { printf("ping: cannot resolve %s\n", host); return 1; }
    struct in_addr da; da.s_addr = dst;
    printf("PING %s (%s) 32 bytes of data\n", host, inet_ntoa(da));

    int fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (fd < 0) { printf("ping: raw socket failed (need privileges?)\n"); return 1; }
    long fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);

    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = dst;

    int sent = 0, recvd = 0;
    for (int seq = 1; seq <= count; seq++) {
        uint8_t msg[40];
        memset(msg, 0, sizeof(msg));
        msg[0] = 8;
        msg[4] = 0x12; msg[5] = 0x34;
        msg[6] = (uint8_t)(seq >> 8); msg[7] = (uint8_t)seq;
        for (int i = 0; i < 32; i++) msg[8 + i] = (uint8_t)('a' + (i % 26));
        uint16_t c = icmp_csum(msg, sizeof(msg));
        msg[2] = (uint8_t)(c >> 8); msg[3] = (uint8_t)c;

        uint64_t t0 = cervus_uptime_ns();
        sendto(fd, msg, sizeof(msg), 0, (struct sockaddr *)&to, sizeof(to));
        sent++;

        int got = 0;
        for (int t = 0; t < 200; t++) {
            uint8_t r[1500];
            struct sockaddr_in from;
            socklen_t fl2 = sizeof(from);
            long n = recvfrom(fd, r, sizeof(r), 0, (struct sockaddr *)&from, &fl2);
            if (n >= 8 && r[0] == 0) {
                int rseq = (r[6] << 8) | r[7];
                uint64_t rtt = cervus_uptime_ns() - t0;
                unsigned ms = (unsigned)(rtt / 1000000ull);
                unsigned us = (unsigned)((rtt / 1000ull) % 1000ull);
                struct in_addr fa; fa.s_addr = from.sin_addr.s_addr;
                printf("%ld bytes from %s: icmp_seq=%d time=%u.%03u ms\n",
                       n, inet_ntoa(fa), rseq, ms, us);
                got = 1; recvd++;
                break;
            }
            usleep(5000);
        }
        if (!got) printf("request timeout for icmp_seq=%d\n", seq);
        if (seq < count) sleep(1);
    }

    printf("--- %s ping statistics ---\n%d packets transmitted, %d received\n",
           host, sent, recvd);
    close(fd);
    return recvd ? 0 : 1;
}
