#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <sys/cervus.h>
#include <cervus_util.h>

typedef struct { const char *name; int num; } sig_t;
static const sig_t SIGS[] = {
    { "HUP",   1 }, { "INT",   2 }, { "QUIT",  3 }, { "ILL",    4 },
    { "TRAP",  5 }, { "ABRT",  6 }, { "BUS",   7 }, { "FPE",    8 },
    { "KILL",  9 }, { "USR1", 10 }, { "SEGV", 11 }, { "USR2",  12 },
    { "PIPE", 13 }, { "ALRM", 14 }, { "TERM", 15 }, { "STKFLT", 16 },
    { "CHLD", 17 }, { "CONT", 18 }, { "STOP", 19 }, { "TSTP",  20 },
    { "TTIN", 21 }, { "TTOU", 22 }, { "URG",  23 }, { "XCPU",  24 },
    { "XFSZ", 25 }, { "VTALRM", 26 }, { "PROF", 27 }, { "WINCH", 28 },
    { "IO",   29 }, { "PWR",  30 }, { "SYS",  31 },
    { NULL,    0 }
};

static int signal_lookup(const char *s)
{
    char up[16];
    size_t n = 0;
    for (; s[n] && n < sizeof up - 1; n++) up[n] = (char)toupper((unsigned char)s[n]);
    up[n] = 0;
    if (s[n]) return -1;
    const char *p = up;
    if (!strncmp(p, "SIG", 3)) p += 3;
    for (int i = 0; SIGS[i].name; i++) if (!strcmp(SIGS[i].name, p)) return SIGS[i].num;
    if (!*p) return -1;
    for (const char *q = p; *q; q++) if (!isdigit((unsigned char)*q)) return -1;
    int v = atoi(p);
    return v < 64 ? v : -1;
}

static const char *signal_name(int num)
{
    for (int i = 0; SIGS[i].name; i++) if (SIGS[i].num == num) return SIGS[i].name;
    return NULL;
}

static void list_signals(void)
{
    for (int i = 0; SIGS[i].name; i++)
        printf("%2d) SIG%-7s%s", SIGS[i].num, SIGS[i].name, (i % 4 == 3) ? "\n" : " ");
    putchar('\n');
}

static const char USAGE[] =
    "Usage: kill [-s SIG | -SIG | -n NUM] target ...\n"
    "       kill -l [SIG]\n"
    "Send a signal to processes (TERM unless told otherwise).\n\n"
    "  -s SIG   the signal, by name or number: -s KILL, -s 9\n"
    "  -SIG     the same: -9, -KILL, -kill, -SIGKILL\n"
    "  -n NUM   the signal by number\n"
    "  -l       list signal names; -l 9 prints the name of signal 9\n\n"
    "A target is a pid, -PGID for a whole process group, or a process name,\n"
    "which reaches every process of that name you are allowed to signal.\n"
    "In csh, %N means the pid of job N.\n";

static void usage(void) { fputs(USAGE, stderr); }

static int is_number(const char *s)
{
    if (!*s) return 0;
    for (; *s; s++) if (!isdigit((unsigned char)*s)) return 0;
    return 1;
}

static int dangerous(pid_t pid, const cervus_task_info_t *info, const char **reason)
{
    *reason = NULL;
    if (pid == 1)
        *reason = "pid 1 is init - killing it will halt the system";
    else if (info->ppid == 0 && info->uid == 0)
        *reason = "this is a kernel thread - terminating it can destabilize the OS";
    return *reason != NULL;
}

static int send_one(pid_t pid, int sig, const cervus_task_info_t *info)
{
    const char *reason;
    if (getuid() == 0 && sig != 0 && dangerous(pid, info, &reason)) {
        char target[80];
        snprintf(target, sizeof target, "send SIG%s to pid %d (%s)",
                 signal_name(sig) ? signal_name(sig) : "?", (int)pid, info->name);
        if (!cervus_confirm(target, NULL, reason)) {
            fputs("kill: aborted\n", stderr);
            return 1;
        }
        if (pid != 1 && info->ppid == 0 && (sig == SIGKILL || sig == SIGTERM)) {
            if (cervus_task_kill(pid) < 0) {
                fprintf(stderr, "kill: (%d) %s: %s\n", (int)pid, info->name, strerror(errno));
                return 1;
            }
            return 0;
        }
    }
    if (kill(pid, sig) < 0) {
        fprintf(stderr, "kill: (%d) %s: %s\n", (int)pid, info->name, strerror(errno));
        return 1;
    }
    return 0;
}

static int kill_pid(pid_t pid, int sig)
{
    cervus_task_info_t info;
    if (cervus_task_info(pid, &info) < 0) {
        fprintf(stderr, "kill: (%d): no such process\n", (int)pid);
        return 1;
    }
    return send_one(pid, sig, &info);
}

static int kill_name(const char *name, int sig)
{
    DIR *d = opendir("/proc");
    if (!d) { fprintf(stderr, "kill: cannot read /proc\n"); return 1; }
    pid_t self = getpid();
    int matched = 0, failed = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (!is_number(de->d_name)) continue;
        pid_t pid = (pid_t)atoi(de->d_name);
        if (pid == self) continue;
        cervus_task_info_t info;
        if (cervus_task_info(pid, &info) < 0) continue;
        if (strcmp(info.name, name) != 0) continue;
        matched++;
        if (send_one(pid, sig, &info) != 0) failed++;
    }
    closedir(d);
    if (!matched) {
        fprintf(stderr, "kill: %s: no process with that name\n", name);
        return 1;
    }
    return failed ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "kill")) return 0;

    int sig = SIGTERM;
    int i = 1;
    if (i < argc && !strcmp(argv[i], "-l")) {
        if (i + 1 >= argc) { list_signals(); return 0; }
        int rc = 0;
        for (i++; i < argc; i++) {
            if (is_number(argv[i])) {
                int n = atoi(argv[i]);
                if (n > 128) n -= 128;
                const char *nm = signal_name(n);
                if (!nm) { fprintf(stderr, "kill: unknown signal %s\n", argv[i]); rc = 1; continue; }
                puts(nm);
            } else {
                int n = signal_lookup(argv[i]);
                if (n < 0 || !signal_name(n)) { fprintf(stderr, "kill: unknown signal '%s'\n", argv[i]); rc = 1; continue; }
                printf("%d\n", n);
            }
        }
        return rc;
    }
    if (i < argc && (!strcmp(argv[i], "-s") || !strcmp(argv[i], "-n"))) {
        if (i + 1 >= argc) { usage(); return 1; }
        sig = signal_lookup(argv[i + 1]);
        if (sig < 0) { fprintf(stderr, "kill: unknown signal '%s'\n", argv[i + 1]); return 1; }
        i += 2;
    } else if (i < argc && argv[i][0] == '-' && argv[i][1] && strcmp(argv[i], "--")) {
        sig = signal_lookup(argv[i] + 1);
        if (sig < 0) { fprintf(stderr, "kill: unknown signal '%s' (see kill -l)\n", argv[i] + 1); return 1; }
        i++;
    }
    if (i < argc && !strcmp(argv[i], "--")) i++;
    if (i >= argc) { usage(); return 1; }

    int rc = 0;
    for (; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] == '%') {
            fprintf(stderr, "kill: %s: job numbers are understood by csh, not here\n", a);
            rc = 1;
        } else if (a[0] == '-' && is_number(a + 1)) {
            if (kill(-atoi(a + 1), sig) < 0) {
                fprintf(stderr, "kill: (%s): %s\n", a, strerror(errno));
                rc = 1;
            }
        } else if (is_number(a)) {
            pid_t pid = (pid_t)atoi(a);
            if (pid <= 0) { fprintf(stderr, "kill: invalid pid '%s'\n", a); rc = 1; continue; }
            rc |= kill_pid(pid, sig);
        } else {
            rc |= kill_name(a, sig);
        }
    }
    return rc;
}
