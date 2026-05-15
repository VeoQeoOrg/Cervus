#include <stdio.h>
#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include <libcervus.h>

FILE *popen(const char *cmd, const char *type)
{
    if (!cmd || !type) { __cervus_errno = EINVAL; return NULL; }
    int fds[2];
    if (pipe(fds) < 0) return NULL;
    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return NULL;
    }
    if (pid == 0) {
        if (type[0] == 'r') { dup2(fds[1], 1); }
        else                { dup2(fds[0], 0); }
        close(fds[0]);
        close(fds[1]);
        char *argv[] = { "/bin/sh", "-c", (char *)cmd, NULL };
        execve("/bin/sh", argv, NULL);
        _exit(127);
    }
    if (type[0] == 'r') {
        close(fds[1]);
        return fdopen(fds[0], "r");
    } else {
        close(fds[0]);
        return fdopen(fds[1], "w");
    }
}
