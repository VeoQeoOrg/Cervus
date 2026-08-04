#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>

#define PORT 9912

static int server(void) {
    int ls = socket(AF_INET6, SOCK_STREAM, 0);
    struct sockaddr_in6 a; memset(&a, 0, sizeof a);
    a.sin6_family = AF_INET6; a.sin6_port = htons(PORT); a.sin6_addr = in6addr_any;
    if (bind(ls, (struct sockaddr *)&a, sizeof a) < 0 || listen(ls, 4) < 0) _exit(2);
    struct sockaddr_in6 ra; socklen_t rl = sizeof ra;
    int c = accept(ls, (struct sockaddr *)&ra, &rl);
    if (c < 0) _exit(3);
    char buf[128];
    long n = recv(c, buf, sizeof buf, 0);
    if (n > 0) send(c, buf, n, 0);
    close(c); close(ls);
    _exit(0);
}

int main(void) {
    pid_t pid = fork();
    if (pid == 0) server();
    usleep(300000);

    int s = socket(AF_INET6, SOCK_STREAM, 0);
    struct sockaddr_in6 sa; memset(&sa, 0, sizeof sa);
    sa.sin6_family = AF_INET6; sa.sin6_port = htons(PORT);
    inet_pton(AF_INET6, "::1", &sa.sin6_addr);
    printf("connecting to [::1]:%d ...\n", PORT);
    if (connect(s, (struct sockaddr *)&sa, sizeof sa) < 0) { printf("connect FAILED\n"); return 1; }
    printf("connected\n");

    const char *msg = "tcp6-echo-42";
    send(s, msg, strlen(msg), 0);
    char buf[128];
    long n = recv(s, buf, sizeof buf, 0);
    buf[n > 0 ? n : 0] = 0;
    int ok = (n == (long)strlen(msg)) && !strcmp(buf, msg);
    printf("sent '%s', got '%s'  %s\n", msg, buf, ok ? "OK" : "FAIL");
    close(s);
    int st; waitpid(pid, &st, 0);
    printf("\n%s\n", ok ? "IPv6 TCP WORKS (::1)" : "IPv6 TCP FAILED");
    return ok ? 0 : 1;
}
