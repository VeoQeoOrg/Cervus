#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/wait.h>
#include <sys/cervus.h>
#include <sys/syscall.h>
#include <cervus_util.h>

#define TIOCSNONBLOCK 0x5481

static const char USAGE[] =
    "Usage: watch [-n seconds] command [args...]\n"
    "Run command repeatedly, showing output fullscreen. Press q to quit.\n";

int main(int argc, char **argv) {
    if (cervus_check_help_version(argc, argv, USAGE, "watch")) return 0;

    double interval = 2.0;
    int i = 1;
    while (i < argc && argv[i][0] == '-') {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            interval = atof(argv[i + 1]);
            if (interval < 0.1) interval = 0.1;
            i += 2;
        } else if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        } else {
            break;
        }
    }

    if (i >= argc) {
        fputs(USAGE, stderr);
        return 1;
    }

    char cmd[1024];
    int o = 0;
    for (int j = i; j < argc && o < (int)sizeof(cmd) - 1; j++) {
        if (j > i && o < (int)sizeof(cmd) - 1) cmd[o++] = ' ';
        const char *a = argv[j];
        while (*a && o < (int)sizeof(cmd) - 1) cmd[o++] = *a++;
    }
    cmd[o] = '\0';

    struct termios orig, raw;
    int have_tio = (tcgetattr(0, &orig) == 0);
    if (have_tio) {
        raw = orig;
        raw.c_lflag &= ~(ICANON | ECHO | ISIG);
        raw.c_cc[VMIN] = 0; raw.c_cc[VTIME] = 0;
        tcsetattr(0, TCSANOW, &raw);
    }
    int nb = 1;
    syscall3(SYS_IOCTL, 0, TIOCSNONBLOCK, &nb);

    uint64_t interval_ns = (uint64_t)(interval * 1000000000.0);
    int running = 1;

    while (running) {
        fputs("\x1b[2J\x1b[H", stdout);
        printf("\x1b[7m Every %.1fs: %s \x1b[0m\n\n", interval, cmd);
        fflush(stdout);

        pid_t pid = fork();
        if (pid == 0) {
            char *cargv[4] = { "csh", "-c", cmd, NULL };
            execvp("csh", cargv);
            _exit(127);
        }
        if (pid < 0) break;

        for (;;) {
            char c;
            while (read(0, &c, 1) == 1) {
                if (c == 'q' || c == 'Q' || c == 3) { running = 0; break; }
            }
            if (!running) break;
            int st;
            pid_t r = waitpid(pid, &st, WNOHANG);
            if (r == pid || r < 0) break;
            cervus_nanosleep(20000000ULL);
        }

        if (!running) {
            cervus_task_kill(pid);
            int st;
            waitpid(pid, &st, 0);
            break;
        }

        uint64_t waited = 0;
        while (waited < interval_ns) {
            char c;
            if (read(0, &c, 1) == 1) {
                if (c == 'q' || c == 'Q' || c == 3) { running = 0; break; }
            }
            cervus_nanosleep(50000000ULL);
            waited += 50000000ULL;
        }
    }

    int z = 0;
    syscall3(SYS_IOCTL, 0, TIOCSNONBLOCK, &z);
    if (have_tio) tcsetattr(0, TCSANOW, &orig);
    fputs("\x1b[2J\x1b[H", stdout);
    fflush(stdout);
    return 0;
}
