#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>

#define PORT 9911

static int server(void) {
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons(PORT); a.sin_addr.s_addr = INADDR_ANY;
    if (bind(ls, (struct sockaddr *)&a, sizeof a) < 0 || listen(ls, 4) < 0) { _exit(2); }
    int c = accept(ls, 0, 0);
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

    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET; sa.sin_port = htons(PORT); sa.sin_addr.s_addr = inet_addr("127.0.0.1");
    printf("connecting to 127.0.0.1:%d ...\n", PORT);
    if (connect(s, (struct sockaddr *)&sa, sizeof sa) < 0) { printf("connect FAILED\n"); return 1; }
    printf("connected\n");

    const char *msg = "loopback-echo-42";
    send(s, msg, strlen(msg), 0);
    char buf[128];
    long n = recv(s, buf, sizeof buf, 0);
    buf[n > 0 ? n : 0] = 0;
    int ok = (n == (long)strlen(msg)) && !strcmp(buf, msg);
    printf("sent '%s', got '%s'  %s\n", msg, buf, ok ? "OK" : "FAIL");
    close(s);
    int st; waitpid(pid, &st, 0);
    printf("\n%s\n", ok ? "LOOPBACK WORKS (127.0.0.1)" : "LOOPBACK FAILED");
    return ok ? 0 : 1;
}
