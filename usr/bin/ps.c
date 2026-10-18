#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/cervus.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: ps [-ef]\n"
    "Report a snapshot of current processes.\n"
    "\n"
    "  -e   show every process (default)\n"
    "  -f   full format (PID, PPID, UID, state, prio, runtime, name)\n";

static void usage(void) { fputs(USAGE, stderr); }

static const char *state_str(uint32_t s)
{
    switch (s) {
        case 0: return "RUNNING ";
        case 1: return "READY   ";
        case 2: return "BLOCKED ";
        case 3: return "ZOMBIE  ";
        default: return "UNKNOWN ";
    }
}

static void fmt_mem(uint64_t b, char *out, size_t n)
{
    if (b >= 1024ULL * 1024)
        snprintf(out, n, "%llu.%lluM",
                 (unsigned long long)(b / (1024 * 1024)),
                 (unsigned long long)((b % (1024 * 1024)) * 10 / (1024 * 1024)));
    else if (b >= 1024)
        snprintf(out, n, "%lluK", (unsigned long long)(b / 1024));
    else
        snprintf(out, n, "%lluB", (unsigned long long)b);
}

static int cmp_pid(const void *a, const void *b)
{
    pid_t x = *(const pid_t *)a, y = *(const pid_t *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "ps")) return 0;
    argc = cervus_end_of_options(argc, argv);

    int full = 0;
    int opt;
    while ((opt = getopt(argc, argv, "ef")) != -1) {
        switch (opt) {
            case 'e': break;
            case 'f': full = 1; break;
            default: usage(); return 1;
        }
    }

    if (full) {
        fputs("  PID  PPID  UID  STATE    PRIO       MEM  RUNTIME(ns)        NAME\n", stdout);
        fputs("  ---  ----  ---  -------  ----  --------  ----------------   ----------------\n", stdout);
    } else {
        fputs("  PID  STATE    NAME\n", stdout);
        fputs("  ---  -------  ----------------\n", stdout);
    }

    size_t n = 0, cap = 256;
    pid_t *pids = malloc(cap * sizeof *pids);
    DIR *d = opendir("/proc");
    if (!pids || !d) { perror("ps: /proc"); return 1; }
    struct dirent *e;
    while ((e = readdir(d))) {
        char *end;
        long v = strtol(e->d_name, &end, 10);
        if (*end || v <= 0) continue;
        if (n == cap) {
            pid_t *np = realloc(pids, cap * 2 * sizeof *pids);
            if (!np) break;
            pids = np;
            cap *= 2;
        }
        pids[n++] = (pid_t)v;
    }
    closedir(d);
    qsort(pids, n, sizeof *pids, cmp_pid);

    for (size_t i = 0; i < n; i++) {
        cervus_task_info_t info;
        if (cervus_task_info(pids[i], &info) < 0) continue;
        if (full) {
            char mem[16];
            fmt_mem(info.rss_bytes, mem, sizeof(mem));
            printf("  %4u  %4u  %3u  %s  %4u  %8s  %16llu   %s\n",
                   (unsigned)info.pid, (unsigned)info.ppid, (unsigned)info.uid,
                   state_str(info.state), (unsigned)info.priority, mem,
                   (unsigned long long)info.total_runtime_ns, info.name);
        } else {
            printf("  %4u  %s  %s\n",
                   (unsigned)info.pid, state_str(info.state), info.name);
        }
    }
    free(pids);
    return 0;
}
