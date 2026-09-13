#include <stdio.h>
#include <signal.h>
#include <string.h>
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

static volatile int g_stop = 0;
static int g_sent = 0, g_recvd = 0;
static char g_host[128];
static uint64_t g_rtt_min = ~0ull, g_rtt_max = 0, g_rtt_sum = 0;

static void on_interrupt(int sig) { (void)sig; g_stop = 1; }

static void print_stats(void) {
    printf("\n--- %s ping statistics ---\n", g_host);
    int lost = g_sent - g_recvd;
    int pct = g_sent ? (lost * 100) / g_sent : 0;
    printf("%d packets transmitted, %d received, %d%% packet loss\n",
           g_sent, g_recvd, pct);
    if (g_recvd > 0 && g_rtt_max > 0) {
        uint64_t avg = g_rtt_sum / (uint64_t)g_recvd;
        printf("rtt min/avg/max = %llu.%03llu/%llu.%03llu/%llu.%03llu ms\n",
               g_rtt_min / 1000, g_rtt_min % 1000,
               avg / 1000, avg % 1000,
               g_rtt_max / 1000, g_rtt_max % 1000);
    }
}

int main(int argc, char **argv) {
    int count = -1;
    int payload = 32;
    const char *host = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) count = atoi(argv[++i]);
        else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) payload = atoi(argv[++i]);
        else host = argv[i];
    }
    if (!host) { printf("usage: ping [-c count] [-s size] <host>\n"); return 1; }
    if (payload < 0) payload = 0;
    if (payload > 65471) payload = 65471;

    in_addr_t dst = inet_resolve(host);
    if (dst == 0xffffffffu) { printf("ping: cannot resolve %s\n", host); return 1; }
    struct in_addr da; da.s_addr = dst;
    printf("PING %s (%s) %d bytes of data\n", host, inet_ntoa(da), payload);

    int fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (fd < 0) { printf("ping: raw socket failed (need privileges?)\n"); return 1; }
    long fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);

    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = dst;

    size_t rlen = 8 + (size_t)payload + 64;
    uint8_t *msg = malloc(8 + (size_t)payload);
    uint8_t *reply = malloc(rlen);
    if (!msg || !reply) { printf("ping: out of memory\n"); return 1; }

    g_sent = 0; g_recvd = 0;
    snprintf(g_host, sizeof g_host, "%s", host);
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = on_interrupt;
        sigaction(SIGINT, &sa, NULL);
    }

    for (int seq = 1; (count < 0 || seq <= count) && !g_stop; seq++) {
        size_t mlen = 8 + (size_t)payload;
        memset(msg, 0, mlen);
        msg[0] = 8;
        msg[4] = 0x12; msg[5] = 0x34;
        msg[6] = (uint8_t)(seq >> 8); msg[7] = (uint8_t)seq;
        for (int i = 0; i < payload; i++) msg[8 + i] = (uint8_t)('a' + (i % 26));
        uint16_t c = icmp_csum(msg, (int)mlen);
        msg[2] = (uint8_t)(c >> 8); msg[3] = (uint8_t)c;

        uint64_t t0 = cervus_uptime_ns();
        sendto(fd, msg, mlen, 0, (struct sockaddr *)&to, sizeof(to));
        g_sent++;

        int got = 0;
        for (int t = 0; t < 200; t++) {
            struct sockaddr_in from;
            socklen_t fl2 = sizeof(from);
            long n = recvfrom(fd, reply, rlen, 0, (struct sockaddr *)&from, &fl2);
            if (n >= 8 && reply[0] == 0) {
                int rseq = (reply[6] << 8) | reply[7];
                uint64_t rtt = cervus_uptime_ns() - t0;
                unsigned ms = (unsigned)(rtt / 1000000ull);
                unsigned us = (unsigned)((rtt / 1000ull) % 1000ull);
                struct in_addr fa; fa.s_addr = from.sin_addr.s_addr;
                printf("%ld bytes from %s: icmp_seq=%d time=%u.%03u ms\n",
                       n, inet_ntoa(fa), rseq, ms, us);
                got = 1; g_recvd++;
                uint64_t rtt_us = rtt / 1000ull;
                if (rtt_us < g_rtt_min) g_rtt_min = rtt_us;
                if (rtt_us > g_rtt_max) g_rtt_max = rtt_us;
                g_rtt_sum += rtt_us;
                break;
            }
            if (g_stop) break;
            usleep(5000);
        }
        if (!got) printf("request timeout for icmp_seq=%d\n", seq);
        if (!g_stop && (count < 0 || seq < count)) sleep(1);
    }

    print_stats();
    free(msg);
    free(reply);
    close(fd);
    return g_recvd ? 0 : 1;
}
