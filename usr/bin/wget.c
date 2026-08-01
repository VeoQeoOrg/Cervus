#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <tls.h>

int main(int argc, char **argv) {
    if (argc < 2) { printf("usage: wget <url>\n"); return 1; }

    char url[512];
    strncpy(url, argv[1], sizeof(url) - 1);
    url[sizeof(url) - 1] = '\0';

    char *p = url;
    int https = 0;
    if (strncmp(p, "https://", 8) == 0) { https = 1; p += 8; }
    else if (strncmp(p, "http://", 7) == 0) p += 7;

    char host[256], path[256];
    int port = https ? 443 : 80, i = 0;
    while (*p && *p != '/' && *p != ':' && i < 255) host[i++] = *p++;
    host[i] = '\0';
    if (*p == ':') { p++; port = atoi(p); while (*p && *p != '/') p++; }
    if (*p == '/') { strncpy(path, p, sizeof(path) - 1); path[sizeof(path) - 1] = '\0'; }
    else strcpy(path, "/");

    if (!host[0]) { printf("wget: bad url\n"); return 1; }

    in_addr_t ip = inet_resolve(host);
    if (ip == 0xffffffffu) { printf("wget: cannot resolve %s\n", host); return 1; }
    struct in_addr ia; ia.s_addr = ip;
    printf("Connecting to %s (%s) port %d %s...\n", host, inet_ntoa(ia), port, https ? "(TLS) " : "");

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { printf("wget: socket failed\n"); return 1; }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    sa.sin_addr.s_addr = ip;
    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        printf("wget: connect failed\n");
        close(fd);
        return 1;
    }

    tls_conn *tc = 0;
    if (https) {
        tc = tls_client_new(fd, host);
        if (!tc || tls_handshake(tc) != 0) {
            printf("wget: TLS handshake failed: %s\n", tc ? tls_error(tc) : "oom");
            if (tc) tls_free(tc);
            close(fd);
            return 1;
        }
    }

    char req[600];
    int rl = snprintf(req, sizeof(req),
                      "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: Cervus-wget\r\nConnection: close\r\n\r\n",
                      path, host);
    if (https) tls_write(tc, req, (size_t)rl);
    else send(fd, req, (size_t)rl, 0);

    char buf[4096];
    long n, total = 0;
    for (;;) {
        if (https) n = tls_read(tc, buf, sizeof(buf));
        else n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        write(1, buf, (size_t)n);
        total += n;
    }
    printf("\n[wget: %ld bytes received]\n", total);
    if (tc) tls_free(tc);
    close(fd);
    return 0;
}
