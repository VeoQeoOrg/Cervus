#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwutil.h>
#include <sys/wait.h>
#include <sys/syscall.h>

int main(int argc, char **argv) {
    if (argc < 2) { printf("usage: sudo <command> [args...]\n"); return 1; }

    uint32_t myuid = (uint32_t)getuid();
    char pw[256] = {0};

    if (myuid != 0) {
        char uname[64];
        if (pw_lookup_uid(myuid, uname, sizeof(uname), NULL, 0, NULL, 0) != 0)
            snprintf(uname, sizeof(uname), "%u", myuid);
        char prompt[128];
        snprintf(prompt, sizeof(prompt), "[sudo] password for %s: ", uname);
        if (pw_getpass(prompt, pw, sizeof(pw)) < 0) return 1;
    }

    long r = syscall3(SYS_SUDO, (uint64_t)(uintptr_t)pw, 0, 0);
    memset(pw, 0, sizeof(pw));
    if (r != 0) {
        if (r == -1 || r == -13) printf("sudo: authentication failure\n");
        else printf("sudo: not permitted (you are not a sudoer)\n");
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) { printf("sudo: fork failed\n"); return 1; }
    if (pid == 0) {
        execvp(argv[1], &argv[1]);
        printf("sudo: cannot exec '%s'\n", argv[1]);
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return status;
}
