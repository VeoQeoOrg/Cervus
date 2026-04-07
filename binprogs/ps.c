#include "../apps/cervus_user.h"

static const char *state_str(uint32_t s) {
    switch (s) {
        case 0: return "RUNNING ";
        case 1: return "READY   ";
        case 2: return "BLOCKED ";
        case 3: return "ZOMBIE  ";
        default:return "UNKNOWN ";
    }
}

CERVUS_MAIN(ps_main) {
    (void)argc; (void)argv;
    ws("  PID  PPID  UID  STATE    PRIO  NAME\n");
    ws("  ---  ----  ---  -------  ----  ----------------\n");
    uint32_t seen[512]; int nseen = 0;
    for (pid_t pid = 0; pid < 512; pid++) {
        cervus_task_info_t info;
        if (task_info(pid, &info) < 0) continue;
        int dup = 0;
        for (int s = 0; s < nseen; s++) if (seen[s] == info.pid) { dup = 1; break; }
        if (dup) continue;
        if (nseen < 512) seen[nseen++] = info.pid;
        ws("  ");
        print_pad(info.pid,      4); ws("  ");
        print_pad(info.ppid,     4); ws("  ");
        print_pad(info.uid,      3); ws("  ");
        ws(state_str(info.state));   ws("  ");
        print_pad(info.priority, 4); ws("  ");
        ws(info.name); wn();
    }
    exit(0);
}