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

static const struct { const char *name; int num; } SIGS[] = {
    { "HUP", 1 }, { "INT", 2 }, { "QUIT", 3 }, { "ABRT", 6 }, { "KILL", 9 },
    { "USR1", 10 }, { "USR2", 12 }, { "PIPE", 13 }, { "ALRM", 14 }, { "TERM", 15 },
    { "CONT", 18 }, { "STOP", 19 }, { "WINCH", 28 }, { NULL, 0 }
};

static const char USAGE[] =
    "Usage: killall [-q] [-s SIG | -SIG] name ...\n"
    "Send a signal (TERM unless told otherwise) to every process named NAME.\n\n"
    "  -q       quiet, do not complain if no process is found\n"
    "  -s SIG   the signal, by name or number; -9 and -KILL work too\n";

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

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "killall")) return 0;

    int quiet = 0, sig = SIGTERM, i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (!strcmp(argv[i], "--")) { i++; break; }
        if (!strcmp(argv[i], "-q")) { quiet = 1; continue; }
        if (!strcmp(argv[i], "-s")) {
            if (i + 1 >= argc) { fputs(USAGE, stderr); return 1; }
            sig = signal_lookup(argv[++i]);
        } else {
            sig = signal_lookup(argv[i] + 1);
        }
        if (sig < 0) { fprintf(stderr, "killall: unknown signal '%s'\n", argv[i]); return 1; }
    }
    if (i >= argc) { fputs(USAGE, stderr); return 1; }

    pid_t self = getpid();
    int rc = 0;
    for (; i < argc; i++) {
        int matched = 0, sent = 0;
        DIR *d = opendir("/proc");
        if (!d) { fputs("killall: cannot read /proc\n", stderr); return 1; }
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            const char *nm = de->d_name;
            if (!isdigit((unsigned char)nm[0])) continue;
            pid_t pid = (pid_t)atoi(nm);
            if (pid <= 1 || pid == self) continue;
            cervus_task_info_t ti;
            if (cervus_task_info(pid, &ti) < 0 || strcmp(ti.name, argv[i]) != 0) continue;
            matched++;
            if (kill(pid, sig) == 0) sent++;
            else if (!quiet) fprintf(stderr, "killall: (%d) %s: %s\n", (int)pid, ti.name, strerror(errno));
        }
        closedir(d);
        if (!matched && !quiet) fprintf(stderr, "killall: %s: no process found\n", argv[i]);
        if (!sent) rc = 1;
    }
    return rc;
}
