#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/cervus.h>
#include <cervus_util.h>

typedef struct { const char *name; int num; } sig_t;
static const sig_t SIGS[] = {
    { "HUP",  1 }, { "INT",  2 }, { "QUIT", 3 }, { "ILL",  4 },
    { "TRAP", 5 }, { "ABRT", 6 }, { "BUS",  7 }, { "FPE",  8 },
    { "KILL", 9 }, { "USR1", 10 }, { "SEGV", 11 }, { "USR2", 12 },
    { "PIPE", 13 }, { "ALRM", 14 }, { "TERM", 15 },
    { NULL,   0 }
};

static int signal_lookup(const char *s)
{
    if (s[0] == 'S' && s[1] == 'I' && s[2] == 'G') s += 3;
    for (int i = 0; SIGS[i].name; i++) if (strcmp(SIGS[i].name, s) == 0) return SIGS[i].num;
    if (isdigit((unsigned char)s[0])) return atoi(s);
    return -1;
}

static void list_signals(void)
{
    for (int i = 0; SIGS[i].name; i++)
        printf("%2d) SIG%s\n", SIGS[i].num, SIGS[i].name);
}

static const char USAGE[] =
    "Usage: kill [-l] [-s sig|-SIG] pid ...\nSend a signal to processes.\n\n  -l      list known signal names\n  -s SIG  send signal SIG (name or number)\n  -SIG    same as -s SIG (e.g. -9, -KILL)\n";

static void usage(void) { fputs(USAGE, stderr); }

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "kill")) return 0;
    argc = cervus_filter_args(argc, argv);

    int sig = 15;

    int i = 1;
    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        if (strcmp(argv[i], "--") == 0) { i++; break; }
        if (strcmp(argv[i], "-l") == 0) { list_signals(); return 0; }
        if (strcmp(argv[i], "-s") == 0) {
            if (i + 1 >= argc) { usage(); return 1; }
            sig = signal_lookup(argv[i + 1]);
            if (sig < 0) { fprintf(stderr, "kill: bad signal '%s'\n", argv[i + 1]); return 1; }
            i += 2; continue;
        }
        const char *s = argv[i] + 1;
        if (isdigit((unsigned char)s[0]) || (s[0] >= 'A' && s[0] <= 'Z')) {
            sig = signal_lookup(s);
            if (sig < 0) { fprintf(stderr, "kill: bad signal '-%s'\n", s); return 1; }
            i++; continue;
        }
        usage(); return 1;
    }

    if (i >= argc) { usage(); return 1; }
    (void)sig;

    int rc = 0;
    for (; i < argc; i++) {
        long pid = strtol(argv[i], NULL, 10);
        if (pid <= 0) { fprintf(stderr, "kill: invalid pid '%s'\n", argv[i]); rc = 1; continue; }

        cervus_task_info_t info;
        if (cervus_task_info((pid_t)pid, &info) < 0) {
            fprintf(stderr, "kill: no process with pid %ld\n", pid);
            rc = 1; continue;
        }

        const char *reason = NULL;
        if (pid == 1)
            reason = "pid 1 is init — killing it will halt the system";
        else if (info.ppid == 0 && info.uid == 0)
            reason = "this is a kernel/root process — terminating it can destabilize the OS";
        else if (info.uid == 0)
            reason = "this process runs as root — it may hold open resources";

        if (reason) {
            char target[64];
            snprintf(target, sizeof(target), "kill pid %ld (%s)", pid, info.name);
            if (!cervus_confirm(target, NULL, reason)) {
                fputs("kill: aborted\n", stderr);
                rc = 1; continue;
            }
        }

        if (cervus_task_kill((pid_t)pid) < 0) {
            fprintf(stderr, "kill: failed to kill pid %ld\n", pid);
            rc = 1;
        }
    }
    return rc;
}
