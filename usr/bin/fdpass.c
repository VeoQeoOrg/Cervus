#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

#define SOCKPATH "/tmp/fdpass.sock"
#define DATAPATH "/tmp/fdpass.data"
#define CONTENT  "HELLO-FDPASS-WORLD"

static int server(void) {
    int ls = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a; memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX; strncpy(a.sun_path, SOCKPATH, sizeof a.sun_path - 1);
    if (bind(ls, (struct sockaddr *)&a, sizeof a) < 0 || listen(ls, 4) < 0) _exit(2);
    int c = accept(ls, 0, 0);
    if (c < 0) _exit(3);
    int rfd = recvfd(c);
    if (rfd < 0) { const char *m = "recvfd FAILED\n"; write(1, m, strlen(m)); _exit(4); }
    char buf[64]; long n = read(rfd, buf, sizeof buf - 1);
    if (n < 0) n = 0;
    buf[n] = 0;
    printf("child: received fd %d, read from it: '%s'\n", rfd, buf);
    int ok = !strcmp(buf, "FDPASS-WORLD");
    close(rfd); close(c); close(ls);
    _exit(ok ? 0 : 5);
}

int main(void) {
    int fd = open(DATAPATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { printf("cannot create data file\n"); return 1; }
    write(fd, CONTENT, strlen(CONTENT));
    close(fd);

    pid_t pid = fork();
    if (pid == 0) server();
    usleep(300000);

    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un sa; memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX; strncpy(sa.sun_path, SOCKPATH, sizeof sa.sun_path - 1);
    if (connect(s, (struct sockaddr *)&sa, sizeof sa) < 0) { printf("connect FAILED\n"); return 1; }

    int dfd = open(DATAPATH, O_RDONLY);
    char pre[8]; long r = read(dfd, pre, 6); pre[r > 0 ? r : 0] = 0;
    printf("parent: read first '%s', now passing the open fd to child\n", pre);
    if (sendfd(s, dfd) < 0) { printf("sendfd FAILED\n"); return 1; }
    close(dfd);
    close(s);

    int st; waitpid(pid, &st, 0);
    int ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
    printf("\n%s\n", ok ? "FD PASSING WORKS (shared open file across processes)" : "FD PASSING FAILED");
    return ok ? 0 : 1;
}
