#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwutil.h>
#include <sys/syscall.h>

int main(int argc, char **argv) {
    const char *user = (argc >= 2) ? argv[1] : "root";
    uint32_t uid = 0, gid = 0;
    char home[128] = "/", shell[128] = "/bin/csh";

    if (pw_lookup_name(user, &uid, &gid, home, sizeof(home), shell, sizeof(shell)) != 0) {
        printf("su: unknown user '%s'\n", user);
        return 1;
    }
    if (!shell[0]) strcpy(shell, "/bin/csh");

    if (getuid() == 0) {
        if (syscall1(SYS_SETUID, (uint64_t)uid) != 0) {
            printf("su: cannot switch to '%s'\n", user);
            return 1;
        }
    } else {
        char pw[256];
        if (pw_getpass("Password: ", pw, sizeof(pw)) < 0) return 1;
        long r = syscall2(SYS_AUTH, (uint64_t)uid, (uint64_t)(uintptr_t)pw);
        memset(pw, 0, sizeof(pw));
        if (r != 0) { printf("su: authentication failure\n"); return 1; }
    }

    setenv("USER", user, 1);
    setenv("HOME", home, 1);

    char *sh = shell;
    char *sargv[] = { sh, NULL };
    execvp(sh, sargv);
    printf("su: cannot exec %s\n", sh);
    return 1;
}
