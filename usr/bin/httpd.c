#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(int argc, char **argv) {
    int port = (argc > 1) ? atoi(argv[1]) : 8080;

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { printf("httpd: socket failed\n"); return 1; }

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = INADDR_ANY;
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) < 0) { printf("httpd: bind failed\n"); return 1; }
    if (listen(s, 8) < 0) { printf("httpd: listen failed\n"); return 1; }

    printf("httpd: listening on port %d (Ctrl-C to stop)\n", port);

    const char *body =
        "<html><body><h1>Hello from Cervus!</h1>"
        "<p>This page is served by a from-scratch x86_64 OS.</p></body></html>";

    for (;;) {
        struct sockaddr_in cli;
        socklen_t cl = sizeof(cli);
        int c = accept(s, (struct sockaddr *)&cli, &cl);
        if (c < 0) { usleep(100000); continue; }

        char req[1024];
        recv(c, req, sizeof(req) - 1, 0);

        char resp[700];
        int n = snprintf(resp, sizeof(resp),
                         "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n"
                         "Content-Length: %d\r\nConnection: close\r\n\r\n%s",
                         (int)strlen(body), body);
        send(c, resp, (size_t)n, 0);
        close(c);

        struct in_addr ia;
        ia.s_addr = cli.sin_addr.s_addr;
        printf("httpd: served %s\n", inet_ntoa(ia));
    }
    return 0;
}
