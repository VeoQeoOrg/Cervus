#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/cervus.h>
#include <cervus_util.h>

int main(int argc, char **argv)
{
    if (argc < 2) {
        fputs("Usage: kill <pid>\n", stdout);
        return 1;
    }
    const char *pid_arg = NULL;
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        pid_arg = argv[i];
        break;
    }
    if (!pid_arg) {
        fputs("Usage: kill <pid>\n", stdout);
        return 1;
    }

    long pid = strtol(pid_arg, NULL, 10);
    if (pid <= 0) {
        fputs("kill: invalid pid\n", stdout);
        return 1;
    }

    cervus_task_info_t info;
    if (cervus_task_info((pid_t)pid, &info) < 0) {
        printf("kill: no process with pid %ld\n", pid);
        return 1;
    }

    int is_system = (pid == 1) || (info.ppid == 0 && info.uid == 0);
    if (is_system) {
        printf("kill: '%s' (pid %ld) is a system process. Kill anyway? [y/N] ",
               info.name, pid);
        char answer[4];
        int n = 0;
        while (n < 3) {
            char c;
            if (read(0, &c, 1) <= 0) break;
            if (c == '\n' || c == '\r') { putchar('\n'); break; }
            putchar(c);
            answer[n++] = c;
        }
        answer[n] = '\0';
        if (n == 0 || (answer[0] != 'y' && answer[0] != 'Y')) {
            fputs("kill: aborted\n", stdout);
            return 0;
        }
    }

    int r = cervus_task_kill((pid_t)pid);
    if (r < 0) {
        printf("kill: failed to kill pid %ld\n", pid);
        return 1;
    }
    return 0;
}
