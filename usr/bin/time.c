#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/cervus.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: time command [args...]\nRun COMMAND and report elapsed real time.\n";

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "time")) return 0;
    if (argc < 2) { fputs(USAGE, stderr); return 1; }

    uint64_t t0 = cervus_uptime_ns();

    pid_t pid = fork();
    if (pid == 0) {
        execvp(argv[1], &argv[1]);
        fprintf(stderr, "time: cannot run '%s'\n", argv[1]);
        _exit(127);
    }
    if (pid < 0) { fputs("time: fork failed\n", stderr); return 1; }

    int st = 0;
    waitpid(pid, &st, 0);

    uint64_t dt = cervus_uptime_ns() - t0;
    uint64_t runtime_ns = 0;
    cervus_task_info_t ti;
    if (cervus_task_info(pid, &ti) == 0) runtime_ns = ti.total_runtime_ns;

    fprintf(stderr, "\nreal\t%llu.%03llus\n",
            (unsigned long long)(dt / 1000000000ULL),
            (unsigned long long)((dt / 1000000ULL) % 1000ULL));
    if (runtime_ns)
        fprintf(stderr, "cpu\t%llu.%03llus\n",
                (unsigned long long)(runtime_ns / 1000000000ULL),
                (unsigned long long)((runtime_ns / 1000000ULL) % 1000ULL));

    return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
}
