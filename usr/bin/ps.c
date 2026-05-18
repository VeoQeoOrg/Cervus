#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "ps")) return 0;
    argc = cervus_filter_args(argc, argv);

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
        fputs("  PID  PPID  UID  STATE    PRIO  RUNTIME(ns)        NAME\n", stdout);
        fputs("  ---  ----  ---  -------  ----  ----------------   ----------------\n", stdout);
    } else {
        fputs("  PID  STATE    NAME\n", stdout);
        fputs("  ---  -------  ----------------\n", stdout);
    }

    uint32_t seen[512];
    int nseen = 0;
    for (pid_t pid = 0; pid < 512; pid++) {
        cervus_task_info_t info;
        if (cervus_task_info(pid, &info) < 0) continue;
        int dup = 0;
        for (int s = 0; s < nseen; s++) if (seen[s] == info.pid) { dup = 1; break; }
        if (dup) continue;
        if (nseen < 512) seen[nseen++] = info.pid;
        if (full) {
            printf("  %4u  %4u  %3u  %s  %4u  %16llu   %s\n",
                   (unsigned)info.pid, (unsigned)info.ppid, (unsigned)info.uid,
                   state_str(info.state), (unsigned)info.priority,
                   (unsigned long long)info.total_runtime_ns, info.name);
        } else {
            printf("  %4u  %s  %s\n",
                   (unsigned)info.pid, state_str(info.state), info.name);
        }
    }
    return 0;
}
