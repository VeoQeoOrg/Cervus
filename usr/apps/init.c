#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/cervus.h>
#include <stddef.h>

#define NVT 12

static int vt_pid[NVT];

static void spawn_shell(int vt) {
    int pid = fork();
    if (pid < 0) {
        cervus_vt_clear_shell(vt);
        return;
    }
    if (pid == 0) {
        cervus_vt_set_ctty(vt);
        close(0);
        close(1);
        close(2);
        open("/dev/tty", O_RDONLY, 0);
        open("/dev/tty", O_WRONLY, 0);
        open("/dev/tty", O_WRONLY, 0);
        char *argv[] = { "/bin/shell", NULL };
        execve("/bin/shell", argv, NULL);
        exit(127);
    }
    vt_pid[vt] = pid;
}

int main(void) {
    for (int i = 0; i < NVT; i++) vt_pid[i] = 0;

    spawn_shell(0);

    for (;;) {
        int status;
        int d;
        while ((d = waitpid(-1, &status, WNOHANG)) > 0) {
            for (int i = 0; i < NVT; i++) {
                if (vt_pid[i] == d) {
                    vt_pid[i] = 0;
                    if (i == 0) spawn_shell(0);
                    else        cervus_vt_clear_shell(i);
                    break;
                }
            }
        }

        int n = cervus_vt_spawn_poll();
        if (n >= 0 && n < NVT && vt_pid[n] == 0) {
            spawn_shell(n);
            continue;
        }

        usleep(16000);
    }
    return 0;
}
