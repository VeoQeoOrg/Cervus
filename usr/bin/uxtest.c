#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

#define SOCKPATH "/tmp/uxtest.sock"

static int server(void) {
    int ls = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ls < 0) _exit(2);
    struct sockaddr_un a; memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    strncpy(a.sun_path, SOCKPATH, sizeof a.sun_path - 1);
    if (bind(ls, (struct sockaddr *)&a, sizeof a) < 0) _exit(3);
    if (listen(ls, 4) < 0) _exit(4);
    int c = accept(ls, 0, 0);
    if (c < 0) _exit(5);
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

    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un sa; memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, SOCKPATH, sizeof sa.sun_path - 1);
    printf("connecting to %s ...\n", SOCKPATH);
    if (connect(s, (struct sockaddr *)&sa, sizeof sa) < 0) { printf("connect FAILED\n"); return 1; }
    printf("connected\n");

    const char *msg = "af_unix-echo-99";
    send(s, msg, strlen(msg), 0);
    char buf[128];
    long n = recv(s, buf, sizeof buf, 0);
    buf[n > 0 ? n : 0] = 0;
    int ok = (n == (long)strlen(msg)) && !strcmp(buf, msg);
    printf("sent '%s', got '%s'  %s\n", msg, buf, ok ? "OK" : "FAIL");
    close(s);
    int st; waitpid(pid, &st, 0);
    printf("\n%s\n", ok ? "AF_UNIX WORKS" : "AF_UNIX FAILED");
    return ok ? 0 : 1;
}
