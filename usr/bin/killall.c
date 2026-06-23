#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/cervus.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: killall [-q] name ...\nKill all processes matching NAME.\n\n  -q   quiet, do not complain if no process found\n";

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "killall")) return 0;

    int quiet = 0;
    int opt;
    while ((opt = getopt(argc, argv, "q")) != -1) {
        if (opt == 'q') quiet = 1;
        else { fputs(USAGE, stderr); return 1; }
    }
    if (optind >= argc) { fputs(USAGE, stderr); return 1; }

    pid_t self = getpid();
    int rc = 0;
    for (int i = optind; i < argc; i++) {
        int killed = 0;
        for (uint32_t pid = 2; pid < 1024; pid++) {
            cervus_task_info_t ti;
            if ((pid_t)pid == self) continue;
            if (cervus_task_info((pid_t)pid, &ti) < 0) continue;
            if (strcmp(ti.name, argv[i]) != 0) continue;
            if (cervus_task_kill((pid_t)pid) == 0) killed++;
        }
        if (!killed && !quiet) {
            fprintf(stderr, "killall: %s: no process found\n", argv[i]);
            rc = 1;
        }
    }
    return rc;
}
