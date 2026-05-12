#include <stdio.h>
#include <sys/cervus.h>

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
    (void)argc; (void)argv;
    fputs("  PID  PPID  UID  STATE    PRIO  NAME\n", stdout);
    fputs("  ---  ----  ---  -------  ----  ----------------\n", stdout);
    uint32_t seen[512];
    int nseen = 0;
    for (pid_t pid = 0; pid < 512; pid++) {
        cervus_task_info_t info;
        if (cervus_task_info(pid, &info) < 0) continue;
        int dup = 0;
        for (int s = 0; s < nseen; s++)
            if (seen[s] == info.pid) { dup = 1; break; }
        if (dup) continue;
        if (nseen < 512) seen[nseen++] = info.pid;
        printf("  %4u  %4u  %3u  %s  %4u  %s\n",
               (unsigned)info.pid, (unsigned)info.ppid, (unsigned)info.uid,
               state_str(info.state), (unsigned)info.priority, info.name);
    }
    return 0;
}
