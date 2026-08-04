#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/wait.h>

int main(void) {
    int fails = 0;

    int pf[2];
    if (pipe(pf) < 0) { printf("pipe failed\n"); return 1; }

    pid_t pid = fork();
    if (pid == 0) {
        close(pf[0]);
        usleep(400000);
        write(pf[1], "hello", 5);
        usleep(100000);
        close(pf[1]);
        _exit(0);
    }
    close(pf[1]);

    struct pollfd p = { pf[0], POLLIN, 0 };
    int r = poll(&p, 1, 100);
    printf("poll empty (100ms timeout): r=%d revents=%d  %s\n", r, p.revents, r == 0 ? "OK (timed out)" : "FAIL");
    if (r != 0) fails++;

    p.revents = 0;
    r = poll(&p, 1, 2000);
    int got_in = (r == 1) && (p.revents & POLLIN);
    printf("poll wait-for-data (2s): r=%d revents=%d  %s\n", r, p.revents, got_in ? "OK (readable)" : "FAIL");
    if (!got_in) fails++;

    char buf[16];
    long n = read(pf[0], buf, sizeof buf);
    buf[n > 0 ? n : 0] = 0;
    printf("read after poll: got %ld bytes '%s'\n", n, buf);

    p.revents = 0;
    r = poll(&p, 1, 2000);
    int got_hup = (r >= 1) && (p.revents & (POLLHUP | POLLIN));
    printf("poll after writer closed: r=%d revents=%d  %s\n", r, p.revents, got_hup ? "OK (hup/eof)" : "FAIL");
    if (!got_hup) fails++;

    close(pf[0]);
    int st; waitpid(pid, &st, 0);

    int sp[2];
    pipe(sp);
    pid = fork();
    if (pid == 0) { close(sp[0]); usleep(300000); write(sp[1], "x", 1); close(sp[1]); _exit(0); }
    close(sp[1]);
    fd_set rd; FD_ZERO(&rd); FD_SET(sp[0], &rd);
    struct timeval tv = { 2, 0 };
    r = select(sp[0] + 1, &rd, NULL, NULL, &tv);
    int sel_ok = (r == 1) && FD_ISSET(sp[0], &rd);
    printf("select wait-for-data (2s): r=%d isset=%d  %s\n", r, FD_ISSET(sp[0], &rd), sel_ok ? "OK" : "FAIL");
    if (!sel_ok) fails++;
    close(sp[0]);
    waitpid(pid, &st, 0);

    printf("\n%s\n", fails == 0 ? "ALL POLL/SELECT TESTS PASSED" : "SOME TESTS FAILED");
    return fails ? 1 : 0;
}
