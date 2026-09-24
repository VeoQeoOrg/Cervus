#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int write_all(int fd, const char *p, size_t n)
{
    while (n) {
        long w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

static int send_all(int sock, const char *p, size_t n)
{
    while (n) {
        long w = send(sock, p, n, 0);
        if (w < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN) {
                struct pollfd pw = { sock, POLLOUT, 0 };
                poll(&pw, 1, 1000);
                continue;
            }
            return -1;
        }
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

static void relay(int sock) {
    char buf[4096];
    int sock_open = 1, stdin_open = 1;
    int interactive = isatty(0);
    while (sock_open || stdin_open) {
        struct pollfd fds[2] = { { sock_open ? sock : -1, POLLIN, 0 }, { stdin_open ? 0 : -1, POLLIN, 0 } };
        if (poll(fds, 2, -1) < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            long n = recv(sock, buf, sizeof(buf), 0);
            if (n > 0) {
                if (write_all(1, buf, (size_t)n) < 0) break;
            } else if (n == 0 || (errno != EAGAIN && errno != EINTR)) {
                sock_open = 0;
                if (interactive) break;
            }
        }
        if (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) {
            long m = read(0, buf, sizeof(buf));
            if (m > 0) {
                if (send_all(sock, buf, (size_t)m) < 0) break;
            } else if (m == 0 || (errno != EAGAIN && errno != EINTR)) {
                stdin_open = 0;
                shutdown(sock, SHUT_WR);
            }
        }
    }
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
            fprintf(stderr, "nc: listening on udp %d\n", port);
            relay(s);
        } else {
            if (listen(s, 1) < 0) { printf("nc: listen failed\n"); return 1; }
            fprintf(stderr, "nc: listening on tcp %d\n", port);
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
