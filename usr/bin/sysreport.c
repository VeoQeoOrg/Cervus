#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <time.h>

extern char **environ;

static const char USAGE[] =
"sysreport - gather everything needed to diagnose this machine\n"
"\n"
"Usage\n"
"  sysreport [file]        write it to a file (default /root/sysreport.txt)\n"
"  sysreport -             write it to standard output\n"
"\n"
"To get the report off this machine in one command, ask for it over ssh\n"
"from the other end:\n"
"\n"
"  ssh root@<this machine> sysreport - > report.txt\n"
"\n"
"Runs the commands someone debugging this system would ask for and\n"
"writes the lot to one file, /root/sysreport.txt unless another path is\n"
"given. What it collects: the kernel build and uptime, processor and\n"
"memory, the PCI and USB devices, which drivers took hold, the disks\n"
"and what is mounted, the network interfaces, and the parts of the\n"
"kernel log that mention a driver, an error or a warning.\n"
"\n"
"Nothing is sent anywhere. It is a file for you to look at or pass on.\n";

typedef struct { const char *title; const char *argv[6]; } step_t;

static const step_t STEPS[] = {
    { "kernel",        { "/bin/uname",    "-a", NULL } },
    { "uptime",        { "/bin/uptime",   NULL } },
    { "processor",     { "/bin/cpuinfo",  NULL } },
    { "memory",        { "/bin/meminfo",  NULL } },
    { "pci devices",   { "/bin/lspci",    NULL } },
    { "usb devices",   { "/bin/lsusb",    NULL } },
    { "drivers",       { "/bin/drv",      NULL } },
    { "block devices", { "/bin/lsblk",    NULL } },
    { "mounted",       { "/bin/mount",    NULL } },
    { "free space",    { "/bin/df",       "-h", NULL } },
    { "network",       { "/bin/ifconfig", NULL } },
};
#define NSTEPS ((int)(sizeof STEPS / sizeof STEPS[0]))

static void banner(int fd, const char *title) {
    char line[128];
    int n = snprintf(line, sizeof line,
                     "\n===== %s =====\n", title);
    write(fd, line, (size_t)n);
}

static int run_into(int fd, const step_t *st) {
    struct stat sb;
    if (stat(st->argv[0], &sb) != 0) {
        char miss[96];
        int n = snprintf(miss, sizeof miss, "(%s is not installed)\n", st->argv[0]);
        write(fd, miss, (size_t)n);
        return 0;
    }

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        dup2(fd, 1);
        dup2(fd, 2);
        execve(st->argv[0], (char *const *)st->argv, environ);
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return 0;
}

static void dmesg_filtered(int fd) {
    banner(fd, "kernel log: drivers, errors and warnings");

    int pipefd[2];
    if (pipe(pipefd) != 0) return;

    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return; }
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], 1);
        close(pipefd[1]);
        const char *argv[2] = { "/bin/dmesg", NULL };
        execve("/bin/dmesg", (char *const *)argv, environ);
        _exit(127);
    }
    close(pipefd[1]);

    static char buf[8192];
    char line[512];
    int li = 0, kept = 0;
    long n;
    while ((n = read(pipefd[0], buf, sizeof buf)) > 0) {
        for (long i = 0; i < n; i++) {
            char c = buf[i];
            if (c != '\n' && li < (int)sizeof line - 1) { line[li++] = c; continue; }
            line[li] = 0;
            int keep = 0;
            static const char *WANT[] = {
                "error", "Error", "ERROR", "fail", "Fail", "FAIL",
                "warn", "Warn", "WARN", "panic", "PANIC",
                "atl1e", "atl1c", "e1000", "ahci", "nvme", "xhci", "ehci",
                "uhci", "hda", "ac97", "bga", "tcp", "dhcp", "smp", "SMP", NULL
            };
            for (int w = 0; WANT[w]; w++)
                if (strstr(line, WANT[w])) { keep = 1; break; }
            if (keep) {
                write(fd, line, strlen(line));
                write(fd, "\n", 1);
                kept++;
            }
            li = 0;
        }
    }
    close(pipefd[0]);
    int status = 0;
    waitpid(pid, &status, 0);

    if (kept == 0) {
        const char *none = "(nothing matched)\n";
        write(fd, none, strlen(none));
    }
}

int main(int argc, char **argv) {
    if (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
        fputs(USAGE, stdout);
        return 0;
    }

    const char *out = (argc > 1) ? argv[1] : "/root/sysreport.txt";
    int to_stdout = (argc > 1 && !strcmp(argv[1], "-"));

    int fd;
    if (to_stdout) {
        fd = 1;
    } else {
        fd = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            fprintf(stderr, "sysreport: cannot write %s\n", out);
            return 1;
        }
    }

    {
        char head[256];
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        int n;
        if (tm)
            n = snprintf(head, sizeof head,
                         "Cervus system report\ncollected %04d-%02d-%02d %02d:%02d:%02d\n",
                         tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                         tm->tm_hour, tm->tm_min, tm->tm_sec);
        else
            n = snprintf(head, sizeof head, "Cervus system report\n");
        write(fd, head, (size_t)n);
    }

    for (int i = 0; i < NSTEPS; i++) {
        if (!to_stdout) { printf("  %-16s", STEPS[i].title); fflush(stdout); }
        banner(fd, STEPS[i].title);
        run_into(fd, &STEPS[i]);
        if (!to_stdout) printf("\x1b[32mok\x1b[0m\n");
    }

    if (!to_stdout) { printf("  %-16s", "kernel log"); fflush(stdout); }
    dmesg_filtered(fd);
    if (!to_stdout) printf("\x1b[32mok\x1b[0m\n");

    if (to_stdout) return 0;

    close(fd);

    struct stat sb;
    unsigned long bytes = (stat(out, &sb) == 0) ? (unsigned long)sb.st_size : 0;
    printf("\nwritten to %s (%lu bytes)\n", out, bytes);
    printf("to get it onto another machine, run this from there:\n");
    printf("  ssh root@<this machine> sysreport - > report.txt\n");
    return 0;
}
