#include "../apps/cervus_user.h"

CERVUS_MAIN(kill_main) {
    if (argc < 2) { ws("Usage: kill <pid>\n"); exit(1); }
    const char *pid_arg = NULL;
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        pid_arg = argv[i]; break;
    }
    if (!pid_arg) { ws("Usage: kill <pid>\n"); exit(1); }

    int64_t pid = atoll(pid_arg);
    if (pid <= 0) { ws("kill: invalid pid\n"); exit(1); }

    cervus_task_info_t info;
    if (task_info((pid_t)pid, &info) < 0) {
        printf("kill: no process with pid %lld\n", (long long)pid);
        exit(1);
    }

    int is_system = (pid == 1) || (info.ppid == 0 && info.uid == 0);
    if (is_system) {
        printf("kill: '%s' (pid %lld) is a system process. Kill anyway? [y/N] ",
               info.name, (long long)pid);
        char answer[4]; int n = 0;
        while (n < 3) {
            char c; if (read(0, &c, 1) <= 0) break;
            if (c == '\n' || c == '\r') { wc('\n'); break; }
            wc(c); answer[n++] = c;
        }
        answer[n] = '\0';
        if (n == 0 || (answer[0] != 'y' && answer[0] != 'Y')) { ws("kill: aborted\n"); exit(0); }
    }

    int r = task_kill((pid_t)pid);
    if (r < 0) { printf("kill: failed to kill pid %lld\n", (long long)pid); exit(1); }
    exit(0);
}