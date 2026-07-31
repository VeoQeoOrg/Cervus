#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define TIOCSNONBLOCK 0x5481

static void tty_nonblock(int on) { int v = on; ioctl(0, TIOCSNONBLOCK, &v); }

static void relay(int sock) {
    long fl = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, fl | O_NONBLOCK);
    tty_nonblock(1);

    char buf[2048];
    int stdin_open = 1;
    for (;;) {
        long n = recv(sock, buf, sizeof(buf), 0);
        if (n > 0) write(1, buf, (size_t)n);
        else if (n == 0) break;

        if (stdin_open) {
            long m = read(0, buf, sizeof(buf));
            if (m > 0) send(sock, buf, (size_t)m, 0);
            else if (m == 0) stdin_open = 0;
        }
        usleep(5000);
    }
    tty_nonblock(0);
}

int main(int argc, char **argv) {
    int udp = 0, listen_mode = 0, ai = 1;
    for (; ai < argc && argv[ai][0] == '-'; ai++) {
        if (strcmp(argv[ai], "-u") == 0) udp = 1;
        else if (strcmp(argv[ai], "-l") == 0) listen_mode = 1;
        else { printf("usage: nc [-u] [-l] [host] <port>\n"); return 1; }
    }

    int type = udp ? SOCK_DGRAM : SOCK_STREAM;

    if (listen_mode) {
        if (ai >= argc) { printf("usage: nc -l <port>\n"); return 1; }
        int port = atoi(argv[ai]);
        int s = socket(AF_INET, type, 0);
        if (s < 0) { printf("nc: socket failed\n"); return 1; }
        struct sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_port = htons((uint16_t)port);
        a.sin_addr.s_addr = INADDR_ANY;
        if (bind(s, (struct sockaddr *)&a, sizeof(a)) < 0) { printf("nc: bind failed\n"); return 1; }
        if (udp) {
            printf("nc: listening on udp %d\n", port);
            relay(s);
        } else {
            if (listen(s, 1) < 0) { printf("nc: listen failed\n"); return 1; }
            printf("nc: listening on tcp %d\n", port);
            struct sockaddr_in cli;
            socklen_t cl = sizeof(cli);
            int c = accept(s, (struct sockaddr *)&cli, &cl);
            if (c < 0) { printf("nc: accept failed\n"); return 1; }
            relay(c);
            close(c);
        }
        close(s);
        return 0;
    }

    if (ai + 1 >= argc) { printf("usage: nc [-u] <host> <port>\n"); return 1; }
    const char *host = argv[ai];
    int port = atoi(argv[ai + 1]);

    in_addr_t ip = inet_resolve(host);
    if (ip == 0xffffffffu) { printf("nc: cannot resolve %s\n", host); return 1; }

    int s = socket(AF_INET, type, 0);
    if (s < 0) { printf("nc: socket failed\n"); return 1; }
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    sa.sin_addr.s_addr = ip;
    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) { printf("nc: connect failed\n"); return 1; }

    relay(s);
    close(s);
    return 0;
}
