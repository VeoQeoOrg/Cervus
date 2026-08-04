#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define NTP_UNIX_DELTA 2208988800UL

int main(int argc, char **argv) {
    const char *host = argc > 1 ? argv[1] : "pool.ntp.org";

    in_addr_t ip = inet_resolve(host);
    if (ip == 0xffffffffu) { fprintf(stderr, "ntpdate: cannot resolve %s\n", host); return 1; }

    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) { fprintf(stderr, "ntpdate: socket failed\n"); return 1; }
    struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET; sa.sin_port = htons(123); sa.sin_addr.s_addr = ip;

    unsigned char pkt[48];
    memset(pkt, 0, sizeof pkt);
    pkt[0] = 0x1b;

    long fl = fcntl(s, F_GETFL, 0); fcntl(s, F_SETFL, fl | O_NONBLOCK);
    if (sendto(s, pkt, sizeof pkt, 0, (struct sockaddr *)&sa, sizeof sa) < 0) {
        fprintf(stderr, "ntpdate: send failed\n"); close(s); return 1;
    }

    struct pollfd p = { s, POLLIN, 0 };
    int r = poll(&p, 1, 3000);
    if (r <= 0) { fprintf(stderr, "ntpdate: no response from %s\n", host); close(s); return 1; }

    unsigned char rsp[48];
    long n = recvfrom(s, rsp, sizeof rsp, 0, 0, 0);
    close(s);
    if (n < 44) { fprintf(stderr, "ntpdate: short reply (%ld bytes)\n", n); return 1; }

    unsigned long ntp_sec = ((unsigned long)rsp[40] << 24) | ((unsigned long)rsp[41] << 16) |
                            ((unsigned long)rsp[42] << 8) | (unsigned long)rsp[43];
    if (ntp_sec < NTP_UNIX_DELTA) { fprintf(stderr, "ntpdate: bad timestamp\n"); return 1; }
    long unix_sec = (long)(ntp_sec - NTP_UNIX_DELTA);

    struct timeval tv = { unix_sec, 0 };
    if (settimeofday(&tv, 0) < 0) { fprintf(stderr, "ntpdate: cannot set clock\n"); return 1; }

    printf("clock set from %s: unix time %ld\n", host, unix_sec);
    printf("run 'date' to see it\n");
    return 0;
}
