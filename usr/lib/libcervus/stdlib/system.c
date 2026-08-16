#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

int system(const char *cmd)
{
    if (!cmd) return 1;
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        char *av[] = { "/bin/csh", "-c", (char *)cmd, NULL };
        execv("/bin/csh", av);
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    return status;
}
